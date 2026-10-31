#!/usr/bin/env python3
"""Fake curl at the process boundary; never contacts a real account."""
import json, os, sys
from pathlib import Path
args=sys.argv[1:]
action=next(x.split('action=')[1] for x in args if 'action=' in x)
mode=os.environ.get('NOTES_TEST_MODE','ok')
assert '--location' not in args
assert '--insecure' not in args
cookie=Path(args[args.index('--cookie-jar')+1])
assert cookie.stat().st_mode & 0o777 == 0o600
assert cookie.parent.stat().st_mode & 0o777 == 0o700
if mode=='offline': sys.exit(6)
if mode=='invalid': print('<html>Bad gateway</html>');sys.exit()
if action=='session': print(json.dumps(dict(ok=True, authenticated=mode!='auth',csrf='test-csrf')));sys.exit()
body=json.load(sys.stdin) if '--data-binary' in args else {}
if body: assert body['csrf']=='test-csrf'
note=dict(id='test-note',type='markdown',content=dict(markdown='żółw'),title='',lifecycle='someday',status='active')
if action=='bootstrap': result=dict(ok=True,notes=[note])
elif action=='save-note':
    assert body['content']['markdown']=='test' and body['lifecycle']=='someday'
    result=dict(ok=True,note=note)
elif action=='login':
    assert body['username']=='tester' and body['password']=='secret "\\\n'
    assert all('secret' not in x for x in args)
    result=dict(ok=True)
else: assert action=='logout';result=dict(ok=True)
print(json.dumps(result))
