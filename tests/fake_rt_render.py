#!/usr/bin/env python3
"""Stand-in for `rt_render --mode app --results FILE`, for tests/test_run_tapp.py.

rt_render's --mode and --results are not wired yet (ROADMAP C9), so this writes the results file
a real app-mode run would, from the fixture app_valid.json. It is a test double, not a model of
rt_render's timing.

Environment:
  FAKE_TAPP_STATE   directory that records one argv_<i>.json per call (call counter and arguments)
  FAKE_TAPP_DATA    tests/data/bench
  FAKE_TAPP_TOTALS  comma-separated app.total_ms, one per call (the call number picks the entry)
  FAKE_TAPP_FAULT   unset, or one of: exit, stdout, nofile, invalid, drift
"""
import json
import os
import sys
from pathlib import Path

state = Path(os.environ['FAKE_TAPP_STATE'])
call = len(list(state.glob('argv_*.json')))
(state / f'argv_{call}.json').write_text(json.dumps({'argv': sys.argv[1:], 'pid': os.getpid()}))

argv = sys.argv[1:]
if argv[argv.index('--mode') + 1] != 'app':
    sys.exit('fake rt_render: only --mode app is supported')
results = Path(argv[argv.index('--results') + 1])

fault = os.environ.get('FAKE_TAPP_FAULT', '')
if fault == 'exit':
    print('fake rt_render: scene failed to load', file=sys.stderr)
    sys.exit(3)
if fault == 'stdout':
    print('progress that belongs on stderr')
if fault == 'nofile':
    sys.exit(0)

totals = [float(t) for t in os.environ['FAKE_TAPP_TOTALS'].split(',')]
total = totals[call]
doc = json.loads((Path(os.environ['FAKE_TAPP_DATA']) / 'app_valid.json').read_text())
doc['app']['total_ms'] = total
doc['app']['components']['render'] = total - 0.5 - 3.25 - 1.0 - 2.0 - 0.5 - 1.75
doc['app']['unattributed_ms'] = 1.75
if fault == 'invalid':
    doc['schema_version'] = 1
if fault == 'drift' and call == 2:
    doc['scene']['hash'] = '0x0000000000000001'
results.write_text(json.dumps(doc))
print('fake rt_render: wrote', results, file=sys.stderr)
