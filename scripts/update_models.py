#!/usr/bin/env python3
"""Check coz's curated AI models (viewer/models.json) against each provider.

When a curated model is no longer listed, it is replaced by (or merged into)
its newest same-family successor, the provider's default moves with it, and
the old ID is added to the registry's 'retired' table so that saved viewer
selections and --model arguments are migrated.

Run weekly by .github/workflows/update-models.yml, which opens a PR with the
result. Locally:

    python3 scripts/update_models.py [--dry-run] [--report FILE]

Providers are checked only when their credentials are set: ANTHROPIC_API_KEY,
OPENAI_API_KEY, and AWS credentials for Bedrock (any boto3 credential source;
needs bedrock:ListInferenceProfiles only). Ollama models are local and aren't
checked. Listing models is free; no prompts are sent.

The script never adds models on its own; newer models it notices are listed
in the report for a human to consider.
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS_JSON = os.path.join(ROOT, 'viewer', 'models.json')

# Spacing between provider requests (provider APIs rate-limit bursts).
REQUEST_GAP_SECONDS = 2.0

DATED = re.compile(r'\d{8}|\d{4}-\d{2}-\d{2}')


def load_coz():
  """The coz script as a module, for its model helpers."""
  loader = SourceFileLoader('coz_cli', os.path.join(ROOT, 'coz'))
  spec = spec_from_loader('coz_cli', loader)
  assert spec is not None
  module = module_from_spec(spec)
  loader.exec_module(module)
  return module


# --- Fetching the live lists --------------------------------------------------

_last_request = [0.0]


def get_json(url, headers):
  for attempt in range(4):
    wait = _last_request[0] + REQUEST_GAP_SECONDS - time.time()
    if wait > 0:
      time.sleep(wait)
    _last_request[0] = time.time()
    try:
      with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as resp:
        return json.load(resp)
    except urllib.error.HTTPError as e:
      if (e.code == 429 or e.code >= 500) and attempt < 3:
        retry_after = e.headers.get('retry-after', '')
        time.sleep(float(retry_after) if retry_after.isdigit() else 10 * (attempt + 1))
        continue
      # Report only the error code: messages can echo part of the key, and
      # workflow logs on a public repo are public.
      code = ''
      try:
        error = json.loads(e.read()).get('error', {})
        code = error.get('code') or error.get('type') or ''
      except Exception:
        pass
      host = urllib.parse.urlparse(url).netloc
      raise RuntimeError(f'{host} returned {e.code}' + (f' ({code})' if code else '')) from None
  raise AssertionError('unreachable')


def list_anthropic(env):
  key = env.get('ANTHROPIC_API_KEY')
  if not key:
    return None
  models, after = [], ''
  while True:
    data = get_json('https://api.anthropic.com/v1/models?limit=1000' + after,
                    {'x-api-key': key, 'anthropic-version': '2023-06-01'})
    models += [{'id': m['id'], 'name': m.get('display_name')} for m in data['data']]
    if not data.get('has_more'):
      return models
    after = '&after_id=' + urllib.parse.quote(data['last_id'])


def list_openai(env, coz):
  key = env.get('OPENAI_API_KEY')
  if not key:
    return None
  data = get_json('https://api.openai.com/v1/models', {'Authorization': f'Bearer {key}'})
  # Only models coz could use (Chat Completions).
  return [{'id': m['id']} for m in data['data'] if coz._is_openai_chat_model(m['id'])]


def list_bedrock(env):
  """The system-defined inference profiles, which is what coz invokes. (Being
  listed doesn't mean a given account may call a model; coz recovers from
  that at request time.)"""
  try:
    import boto3
    import botocore.exceptions
  except ImportError:
    return None
  session = boto3.Session()
  if session.get_credentials() is None:
    return None
  region = env.get('AWS_REGION') or env.get('AWS_DEFAULT_REGION') or 'us-east-1'
  client = session.client('bedrock', region_name=region)
  models, kwargs = [], {'typeEquals': 'SYSTEM_DEFINED', 'maxResults': 1000}
  try:
    while True:
      page = client.list_inference_profiles(**kwargs)
      models += [{'id': p['inferenceProfileId'], 'name': p.get('inferenceProfileName')}
                 for p in page.get('inferenceProfileSummaries', []) if p.get('status') == 'ACTIVE']
      if not page.get('nextToken'):
        return models
      kwargs['nextToken'] = page['nextToken']
  except botocore.exceptions.ClientError as e:
    raise RuntimeError(f"Bedrock returned {e.response.get('Error', {}).get('Code', 'an error')}") from None


# --- Deciding what changes (pure) -----------------------------------------------

def plan_provider(provider, entry, live, coz):
  """For one provider: which curated models are gone, and what replaces them.
  `live` is [{'id', 'name'?}]. Returns a dict with 'changes' ([{provider,
  from, to, label}]), 'unresolved', 'newer' and 'skipped'."""
  live_ids = [m['id'] for m in live]
  live_set = set(live_ids)
  curated = [m['id'] for m in entry['models']]
  missing = [m for m in curated if m not in live_set]
  result = {'changes': [], 'unresolved': [], 'newer': [], 'skipped': None}

  # If most of the list vanished at once, the listing API changed or the key
  # sees a restricted view; don't rewrite the registry on that basis.
  if not live_ids or len(missing) > len(curated) / 2:
    if missing:
      result['skipped'] = (f'{len(missing)} of {len(curated)} curated models are not listed; '
                           'not changing anything')
    return result

  # Prefer aliases (claude-sonnet-5-5, gpt-5.2) over dated snapshots of them.
  undated = [m for m in live_ids if not DATED.search(m)]
  default = entry['default'] if entry['default'] in live_set else ''

  def best(model, available):
    for candidate in coz._rank_replacements(model, available, default):
      if candidate in live_set:
        return candidate
    return None

  for model in missing:
    replacement = best(model, undated) or best(model, live_ids)
    if not replacement:
      result['unresolved'].append(model)
      continue
    name = next((m.get('name') for m in live if m['id'] == replacement), None)
    result['changes'].append({'provider': provider, 'from': model, 'to': replacement,
                              'label': name or replacement})

  # Newer models in a family we curate, for the report only.
  newest = {}
  for model in curated:
    if model in live_set:
      family = coz._model_family(model)
      if family not in newest or coz._compare_model_recency(model, newest[family]) > 0:
        newest[family] = model
  for model in undated:
    family = coz._model_family(model)
    if (family in newest and model not in curated
        and coz._compare_model_recency(model, newest[family]) > 0):
      result['newer'].append(model)
  return result


def apply_changes(registry, changes):
  """Apply changes ({provider, from, to, label}) to the parsed registry."""
  for change in changes:
    provider, old, new = change['provider'], change['from'], change['to']
    entry = registry['providers'][provider]
    if any(m['id'] == new for m in entry['models']):
      entry['models'] = [m for m in entry['models'] if m['id'] != old]
    else:
      for m in entry['models']:
        if m['id'] == old:
          m['id'], m['label'] = new, change['label']
    if entry['default'] == old:
      entry['default'] = new
    # Point existing retirements at the new target, then add this one.
    retired = registry['retired'].setdefault(provider, {})
    for key, value in retired.items():
      if value == old:
        retired[key] = new
    retired[old] = new
  return registry


def dump_registry(registry):
  """models.json in its checked-in layout: one line per model."""
  def q(s):
    return json.dumps(s)

  lines = ['{', '  "_comment": [']
  comments = registry.get('_comment', [])
  lines += [f'    {q(c)}' + (',' if i < len(comments) - 1 else '') for i, c in enumerate(comments)]
  lines += ['  ],', '  "providers": {']
  providers = list(registry['providers'].items())
  for i, (name, entry) in enumerate(providers):
    lines.append(f'    {q(name)}: {{')
    lines.append(f'      "default": {q(entry["default"])},')
    if entry['models']:
      lines.append('      "models": [')
      for j, m in enumerate(entry['models']):
        comma = ',' if j < len(entry['models']) - 1 else ''
        lines.append(f'        {{ "id": {q(m["id"])}, "label": {q(m["label"])} }}{comma}')
      lines.append('      ]')
    else:
      lines.append('      "models": []')
    lines.append('    }' + (',' if i < len(providers) - 1 else ''))
  lines += ['  },', '  "retired": {']
  retired = list(registry['retired'].items())
  for i, (name, table) in enumerate(retired):
    comma = ',' if i < len(retired) - 1 else ''
    if not table:
      lines.append(f'    {q(name)}: {{}}{comma}')
      continue
    lines.append(f'    {q(name)}: {{')
    items = list(table.items())
    lines += [f'      {q(k)}: {q(v)}' + (',' if j < len(items) - 1 else '')
              for j, (k, v) in enumerate(items)]
    lines.append('    }' + comma)
  lines += ['  }', '}']
  return '\n'.join(lines) + '\n'


# --- Main ------------------------------------------------------------------------

def main(argv=None, env=None):
  parser = argparse.ArgumentParser(description=(__doc__ or '').split('\n\n')[0])
  parser.add_argument('--dry-run', action='store_true', help="Report only; don't rewrite models.json.")
  parser.add_argument('--report', metavar='FILE', help='Also write the Markdown report to FILE.')
  args = parser.parse_args(argv)
  env = os.environ if env is None else env

  coz = load_coz()
  with open(MODELS_JSON) as f:
    registry = json.load(f)

  listers = {
    'anthropic': lambda: list_anthropic(env),
    'openai': lambda: list_openai(env, coz),
    'bedrock': lambda: list_bedrock(env),
  }
  changes, report, checked = [], [], 0
  for provider, lister in listers.items():
    try:
      live = lister()
    except Exception as e:
      report.append(f"- **{provider}**: couldn't list models ({str(e).splitlines()[0]})")
      continue
    if live is None:
      report.append(f'- **{provider}**: not checked (no credentials)')
      continue
    checked += 1
    plan = plan_provider(provider, registry['providers'][provider], live, coz)
    changes += plan['changes']
    notes = [f"`{c['from']}` is gone; replaced by `{c['to']}`" for c in plan['changes']]
    notes += [f'`{m}` is gone and has no replacement; **needs a human**' for m in plan['unresolved']]
    if plan['skipped']:
      notes.append(plan['skipped'])
    if plan['newer']:
      notes.append('newer models listed: ' + ', '.join(f'`{m}`' for m in plan['newer']))
    report.append(f'- **{provider}**: ' + ('; '.join(notes) if notes else 'all curated models are listed'))
  report.append('- **ollama**: not checked (local models)')

  if changes:
    header = [
      'These curated models in `viewer/models.json` are no longer listed by their providers.',
      'This PR replaces them with their newest same-family successors and adds the old IDs to',
      "the registry's `retired` table, so saved selections are migrated. Please check the new labels.",
      '',
    ]
  else:
    header = ['All checked curated models are still listed.', '']
  text = '\n'.join(header + report) + '\n'
  sys.stdout.write(text)
  if args.report:
    with open(args.report, 'w') as f:
      f.write(text)

  if changes and not args.dry_run:
    with open(MODELS_JSON, 'w') as f:
      f.write(dump_registry(apply_changes(registry, changes)))
  if checked == 0:
    sys.stderr.write('No provider could be checked (no credentials, or listing failed).\n')
  return 0


if __name__ == '__main__':
  sys.exit(main())
