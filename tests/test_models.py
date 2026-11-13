#!/usr/bin/env python3
"""Tests for the AI model registry (viewer/models.json), the coz script's model
helpers (migration, replacement ranking, recovery), and scripts/update_models.py.

Run with: python3 -m unittest discover -s tests -p 'test_models.py'
"""

import copy
import glob
import json
import os
import re
import sys
import unittest
from importlib.machinery import SourceFileLoader
from importlib.util import module_from_spec, spec_from_loader

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODELS_JSON = os.path.join(ROOT, 'viewer', 'models.json')

sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import update_models  # noqa: E402


def load_coz():
  loader = SourceFileLoader('coz_cli_test', os.path.join(ROOT, 'coz'))
  spec = spec_from_loader('coz_cli_test', loader)
  module = module_from_spec(spec)
  loader.exec_module(module)
  return module


coz = load_coz()


def registry():
  with open(MODELS_JSON) as f:
    return json.load(f)


class RegistryTest(unittest.TestCase):
  def test_providers_match_coz(self):
    self.assertEqual(sorted(registry()['providers']), sorted(coz.MODEL_PROVIDERS))
    self.assertEqual(sorted(registry()['retired']), sorted(coz.MODEL_PROVIDERS))

  def test_defaults_are_curated(self):
    for provider, entry in registry()['providers'].items():
      ids = [m['id'] for m in entry['models']]
      self.assertEqual(len(ids), len(set(ids)), f'duplicate IDs for {provider}')
      self.assertTrue(entry['default'])
      if ids:  # Ollama models are local, so none are curated.
        self.assertIn(entry['default'], ids, provider)
      for m in entry['models']:
        self.assertTrue(m['label'], m['id'])

  def test_retired_models_lead_somewhere_curated(self):
    reg = registry()
    for provider, table in reg['retired'].items():
      curated = {m['id'] for m in reg['providers'][provider]['models']}
      for old in table:
        self.assertNotIn(old, curated, f'{old} is both curated and retired')
        self.assertIn(coz._migrate_model(provider, old), curated, old)

  def test_file_is_in_canonical_layout(self):
    # scripts/update_models.py rewrites the file with dump_registry; keep the
    # checked-in layout identical so its PRs show only real changes.
    with open(MODELS_JSON) as f:
      text = f.read()
    self.assertEqual(update_models.dump_registry(json.loads(text)), text)

  def test_no_model_ids_outside_the_registry(self):
    # Hardcoded IDs go stale when providers retire them; they belong in
    # viewer/models.json only.
    literal = re.compile(
      r'''['"`]((?:[a-z]{2,6}\.)?anthropic\.claude[^'"`]*|claude-[a-z]+-\d[^'"`]*|'''
      r'''gpt-\d[^'"`]*|chatgpt-[^'"`]+|o\d(?:-[a-z]+)?|llama\d[^'"`]*)['"`]''')
    sources = [os.path.join(ROOT, 'coz'), os.path.join(ROOT, 'viewer', 'index.htm')]
    sources += glob.glob(os.path.join(ROOT, 'viewer', 'ts', '*.ts'))
    sources += glob.glob(os.path.join(ROOT, 'viewer', 'js', '*.js'))
    offenders = []
    for path in sources:
      with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
          for match in literal.finditer(line):
            offenders.append(f'{os.path.relpath(path, ROOT)}:{number}: {match.group(1)}')
    self.assertEqual(offenders, [], 'model IDs outside viewer/models.json')


class HelperTest(unittest.TestCase):
  def test_model_family(self):
    cases = {
      'claude-sonnet-4-5-20250929': 'claude-sonnet',
      'claude-sonnet-5-5': 'claude-sonnet',
      'global.anthropic.claude-opus-4-6-v1': 'claude-opus',
      'us.anthropic.claude-haiku-4-5-20251001-v1:0': 'claude-haiku',
      'anthropic.claude-opus-4-20250514-v1:0': 'claude-opus',
      'gpt-5.4-mini': 'gpt-mini',
      'gpt-5.5': 'gpt',
      'o4-mini': 'o4-mini',
    }
    for model, family in cases.items():
      self.assertEqual(coz._model_family(model), family, model)

  def test_recency(self):
    self.assertGreater(coz._compare_model_recency('claude-opus-5-5', 'claude-opus-4-7'), 0)
    self.assertGreater(coz._compare_model_recency('claude-opus-4-7', 'claude-opus-4-5-20251101'), 0)
    self.assertGreater(coz._compare_model_recency('gpt-latest', 'gpt-9'), 0)
    self.assertEqual(coz._compare_model_recency('gpt-5', 'gpt-5'), 0)

  def test_rank_replacements(self):
    available = ['claude-opus-4-7', 'claude-opus-5-5', 'claude-sonnet-5-5']
    self.assertEqual(coz._rank_replacements('claude-opus-4-20250514', available, 'claude-sonnet-5-5'),
                     ['claude-opus-5-5', 'claude-opus-4-7', 'claude-sonnet-5-5'])
    # The default is skipped when the live list shows it is gone.
    self.assertEqual(coz._rank_replacements('o3-mini', ['gpt-5.2'], 'gpt-5.5'), [])
    # ...and never offered in place of itself.
    self.assertEqual(coz._rank_replacements('gpt-5.5', ['gpt-5.5'], 'gpt-5.5'), [])

  def test_migrate_follows_chains(self):
    reg = copy.deepcopy(registry())
    reg['retired']['openai'] = {'a-1': 'a-2', 'a-2': 'a-3', 'loop-1': 'loop-2', 'loop-2': 'loop-1'}
    saved = coz._model_registry_cache
    coz._model_registry_cache = reg
    try:
      self.assertEqual(coz._migrate_model('openai', 'a-1'), 'a-3')
      self.assertEqual(coz._migrate_model('openai', 'a-3'), 'a-3')
      self.assertIn(coz._migrate_model('openai', 'loop-1'), ('loop-1', 'loop-2'))
    finally:
      coz._model_registry_cache = saved

  def test_openai_chat_filter(self):
    self.assertTrue(coz._is_openai_chat_model('gpt-5.5'))
    self.assertTrue(coz._is_openai_chat_model('o3'))
    for model in ('gpt-5.5-pro', 'gpt-4o-mini-tts', 'gpt-5-search-api', 'gpt-5.3-codex',
                  'text-embedding-3-large', 'dall-e-3', 'omni-moderation-latest'):
      self.assertFalse(coz._is_openai_chat_model(model), model)


class ErrorClassificationTest(unittest.TestCase):
  def test_anthropic(self):
    body = json.dumps({'type': 'error', 'error': {'type': 'not_found_error', 'message': 'model: x'}})
    self.assertIsInstance(coz._anthropic_error('x', 404, body), coz.ModelNotFoundError)
    body = json.dumps({'type': 'error', 'error': {'type': 'rate_limit_error', 'message': 'slow down'}})
    error = coz._anthropic_error('x', 429, body)
    self.assertNotIsInstance(error, coz.ModelNotFoundError)
    self.assertIn('429', str(error))

  def test_openai(self):
    body = json.dumps({'error': {'code': 'model_not_found', 'message': 'The model `x` does not exist'}})
    self.assertIsInstance(coz._openai_error('x', 404, body), coz.ModelNotFoundError)
    body = json.dumps({'error': {'code': 'insufficient_quota', 'message': 'quota'}})
    self.assertNotIsInstance(coz._openai_error('x', 429, body), coz.ModelNotFoundError)
    self.assertNotIsInstance(coz._openai_error('x', 500, 'not json'), coz.ModelNotFoundError)

  def test_bedrock(self):
    class FakeClientError(Exception):
      def __init__(self, code, message):
        super().__init__(message)
        self.response = {'Error': {'Code': code, 'Message': message}}

    gone = [
      ('ResourceNotFoundException', 'Model not found'),
      ('ValidationException', 'The provided model identifier is invalid.'),
      ('AccessDeniedException', 'This model version is not available for this account.'),
    ]
    for code, message in gone:
      self.assertIsInstance(coz._bedrock_error('m', FakeClientError(code, message)),
                            coz.ModelNotFoundError, code)
    self.assertNotIsInstance(coz._bedrock_error('m', FakeClientError('ThrottlingException', 'slow')),
                             coz.ModelNotFoundError)


class RecoveryTest(unittest.TestCase):
  def setUp(self):
    self.saved_lister = coz._list_live_models
    coz._list_live_models = lambda provider, **kwargs: [
      {'value': m, 'label': m} for m in
      ('claude-opus-4-7', 'claude-opus-5-5', 'claude-opus-5-5-20261001', 'claude-sonnet-5-5')]

  def tearDown(self):
    coz._list_live_models = self.saved_lister

  def test_switches_to_newest_same_family_alias(self):
    tried, switches = [], []

    def attempt(model):
      tried.append(model)
      if model == 'claude-opus-4-20250514':
        raise coz.ModelNotFoundError('anthropic', model, 'not found')
      return 'answer from ' + model

    result = coz._with_model_recovery('anthropic', 'claude-opus-4-20250514', attempt,
                                      on_switch=lambda old, new: switches.append((old, new)))
    self.assertEqual(result, 'answer from claude-opus-5-5')
    self.assertEqual(tried, ['claude-opus-4-20250514', 'claude-opus-5-5'])
    self.assertEqual(switches, [('claude-opus-4-20250514', 'claude-opus-5-5')])

  def test_gives_up_after_bounded_attempts(self):
    tried = []

    def attempt(model):
      tried.append(model)
      raise coz.ModelNotFoundError('anthropic', model, 'not found')

    with self.assertRaises(coz.ModelNotFoundError) as caught:
      coz._with_model_recovery('anthropic', 'claude-opus-4-20250514', attempt)
    self.assertEqual(caught.exception.model, 'claude-opus-4-20250514')
    self.assertEqual(len(tried), 1 + coz._MAX_MODEL_REPLACEMENTS)

  def test_other_errors_are_not_retried(self):
    tried = []

    def attempt(model):
      tried.append(model)
      raise RuntimeError('Anthropic API 529: overloaded')

    with self.assertRaises(RuntimeError):
      coz._with_model_recovery('anthropic', 'claude-opus-5-5', attempt)
    self.assertEqual(tried, ['claude-opus-5-5'])

  def test_replacement_failing_otherwise_is_not_adopted(self):
    switches = []

    def attempt(model):
      if model == 'claude-opus-4-20250514':
        raise coz.ModelNotFoundError('anthropic', model, 'not found')
      raise RuntimeError('overloaded')

    with self.assertRaises(RuntimeError):
      coz._with_model_recovery('anthropic', 'claude-opus-4-20250514', attempt,
                               on_switch=lambda old, new: switches.append(new))
    self.assertEqual(switches, [])


class UpdateModelsTest(unittest.TestCase):
  def entry(self):
    return {
      'default': 'claude-opus-4-20250514',
      'models': [
        {'id': 'claude-opus-4-20250514', 'label': 'Claude Opus 4'},
        {'id': 'claude-sonnet-5-5', 'label': 'Claude Sonnet 5.5'},
        {'id': 'claude-haiku-5-5', 'label': 'Claude Haiku 5.5'},
      ],
    }

  def test_plan_replaces_missing_and_reports_newer(self):
    live = [{'id': 'claude-opus-5-5', 'name': 'Claude Opus 5.5'},
            {'id': 'claude-opus-5-5-20261001'},
            {'id': 'claude-sonnet-5-5'}, {'id': 'claude-sonnet-6'},
            {'id': 'claude-haiku-5-5'}]
    plan = update_models.plan_provider('anthropic', self.entry(), live, coz)
    self.assertEqual(plan['changes'], [{'provider': 'anthropic', 'from': 'claude-opus-4-20250514',
                                        'to': 'claude-opus-5-5', 'label': 'Claude Opus 5.5'}])
    self.assertEqual(plan['newer'], ['claude-sonnet-6'])
    self.assertIsNone(plan['skipped'])

  def test_plan_refuses_when_most_models_vanish(self):
    plan = update_models.plan_provider('anthropic', self.entry(), [{'id': 'claude-opus-5-5'}], coz)
    self.assertEqual(plan['changes'], [])
    self.assertIsNotNone(plan['skipped'])

  def test_plan_flags_unresolvable(self):
    entry = self.entry()
    entry['models'][0] = {'id': 'claude-mystery-1', 'label': 'Mystery'}
    entry['default'] = 'claude-sonnet-5-5'
    live = [{'id': 'claude-sonnet-5-5'}, {'id': 'claude-haiku-5-5'}]
    plan = update_models.plan_provider('anthropic', entry, live, coz)
    # Falls back to the (still listed) default rather than giving up.
    self.assertEqual([c['to'] for c in plan['changes']], ['claude-sonnet-5-5'])

  def test_apply_changes(self):
    reg = {'providers': {'anthropic': self.entry()},
           'retired': {'anthropic': {'claude-opus-3': 'claude-opus-4-20250514'}}}
    changes = [
      {'provider': 'anthropic', 'from': 'claude-opus-4-20250514', 'to': 'claude-opus-5-5',
       'label': 'Claude Opus 5.5'},
      # Merged into an entry that's already curated.
      {'provider': 'anthropic', 'from': 'claude-haiku-5-5', 'to': 'claude-sonnet-5-5',
       'label': 'Claude Sonnet 5.5'},
    ]
    update_models.apply_changes(reg, changes)
    entry = reg['providers']['anthropic']
    self.assertEqual(entry['default'], 'claude-opus-5-5')
    self.assertEqual([m['id'] for m in entry['models']], ['claude-opus-5-5', 'claude-sonnet-5-5'])
    self.assertEqual(entry['models'][0]['label'], 'Claude Opus 5.5')
    self.assertEqual(reg['retired']['anthropic'], {
      'claude-opus-3': 'claude-opus-5-5',
      'claude-opus-4-20250514': 'claude-opus-5-5',
      'claude-haiku-5-5': 'claude-sonnet-5-5',
    })

  def test_rewritten_registry_is_valid_json(self):
    reg = registry()
    first = reg['providers']['openai']['models'][0]['id']
    update_models.apply_changes(reg, [{'provider': 'openai', 'from': first, 'to': 'gpt-99',
                                       'label': 'GPT-99'}])
    reparsed = json.loads(update_models.dump_registry(reg))
    self.assertEqual(reparsed, reg)
    self.assertEqual(reparsed['retired']['openai'][first], 'gpt-99')


if __name__ == '__main__':
  unittest.main()
