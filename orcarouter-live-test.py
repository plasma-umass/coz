#!/usr/bin/env python3
"""Live test for the OrcaRouter provider added to Coz.

Exercises the real code paths added in the PR:
  1. `_orcarouter_tool_request` + `_suggest_run_orcarouter_agent`
     (the `coz suggest-points` tool-calling agent path)
  2. The `coz plot` viewer `/optimize` streaming path (`_stream_orcarouter`)
  3. The `/orcarouter-models` model-list endpoint

Requires ORCAROUTER_API_KEY to be set (never printed by this script).
"""
import json
import os
import subprocess
import sys
import time
import urllib.request
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
key = os.environ.get('ORCAROUTER_API_KEY', '')
if not key:
    print('FAIL: ORCAROUTER_API_KEY not set')
    sys.exit(1)

results = []


def check(name, ok, detail=''):
    results.append((name, ok, detail))
    print(f'{"PASS" if ok else "FAIL"}: {name} {detail}')


# --- Case 1: tool-calling agent request (suggest-points path) ---
print('\n[Case 1] _orcarouter_tool_request (tool calling)')
import runpy
_mod = runpy.run_path(os.path.join(HERE, 'coz'))
try:
    tools = [
        {
            'name': 'list_files',
            'description': 'List files in the tree.',
            'input_schema': {'type': 'object', 'properties': {'path': {'type': 'string'}}},
        }
    ]
    resp = _mod['_orcarouter_tool_request'](
        key,
        'openai/gpt-4o-mini',
        'You are a test assistant.',
        [{'role': 'user', 'content': 'Call list_files with path="."'}],
        tools,
        max_tokens=512,
    )
    choices = resp.get('choices') or []
    msg = (choices[0].get('message') or {}) if choices else {}
    ok = bool(choices) and ('tool_calls' in msg or 'content' in msg)
    check('tool request returns chat completion', ok, f'choices={len(choices)}')
except Exception as e:
    check('tool request returns chat completion', False, str(e))


# --- Case 2/3: start the real `coz plot` server and hit its endpoints ---
print('\n[Case 2/3] coz plot server: /optimize (stream) + /orcarouter-models')
port = '18423'
proc = subprocess.Popen(
    [sys.executable, os.path.join(HERE, 'coz'), 'plot', '--port', port, '--no-browser'],
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    stdin=subprocess.PIPE,  # keep stdin open so the server keeps serving
    cwd=HERE,
)

try:
    # Wait for the server to come up.
    base = f'http://localhost:{port}'
    for _ in range(50):
        try:
            urllib.request.urlopen(base + '/', timeout=2)
            break
        except Exception:
            time.sleep(0.2)
    else:
        check('server starts', False, 'timed out')
        raise SystemExit(1)

    # /optimize streaming, provider=orcarouter
    body = json.dumps({
        'path': os.path.join(HERE, 'coz'),
        'line': 100,
        'speedup_data': [],
        'provider': 'orcarouter',
        'api_key': key,
        'model': 'openai/gpt-4o-mini',
        'ollama_host': 'http://localhost:11434',
    }).encode('utf-8')
    req = urllib.request.Request(
        base + '/optimize',
        data=body,
        headers={'Content-Type': 'application/json'},
    )
    chunks = []
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            for raw in resp:
                line = raw.decode('utf-8').strip()
                if not line:
                    continue
                try:
                    evt = json.loads(line)
                except json.JSONDecodeError:
                    continue
                chunks.append(evt)
        done = any('done' in c for c in chunks)
        text_chunks = [c.get('chunk', '') for c in chunks if 'chunk' in c]
        ok = done and len(text_chunks) > 0
        check('/optimize streams OrcaRouter reply', ok,
              f'lines={len(chunks)} text={"".join(text_chunks)[:40]!r}')
    except Exception as e:
        check('/optimize streams OrcaRouter reply', False, str(e))

    # /orcarouter-models endpoint
    try:
        with urllib.request.urlopen(
            base + '/orcarouter-models?api_key=' + key, timeout=15
        ) as resp:
            data = json.loads(resp.read())
        ids = [m['value'] for m in data.get('models', [])]
        ok = bool(ids) and 'orcarouter/auto' in ids
        check('/orcarouter-models returns curated models', ok, f'count={len(ids)}')
    except Exception as e:
        check('/orcarouter-models returns curated models', False, str(e))

finally:
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()


print('\n=== SUMMARY ===')
fails = [r for r in results if not r[1]]
print(f'{len(results) - len(fails)}/{len(results)} passed')
sys.exit(1 if fails else 0)
