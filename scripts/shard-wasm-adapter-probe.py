#!/usr/bin/env python3
"""Two real WASM processes, independent synthetic stores: adapter test, NOT sharding acceptance."""
import json
import os
from pathlib import Path
import signal
import statistics
import subprocess
import tempfile
import time
import urllib.request

root = Path(__file__).resolve().parent.parent
node = os.environ.get('NODE', 'node')
accounts = int(os.environ.get('SHARD_BENCH_ACCOUNTS', '0'))
with tempfile.TemporaryDirectory(prefix='shard-wasm-adapter-') as directory:
    base = Path(directory)
    processes = []
    logs = []
    try:
        for index, port in enumerate((3091, 3092)):
            home = base / str(index)
            home.mkdir()
            if accounts:
                fixture = {'users':{}, 'players':{}}
                for n in range(accounts):
                    name = f'bench{n:05d}'
                    fixture['users'][name] = {'id':name, 'username':name, 'passwordHash':'', 'createdAt':0}
                    fixture['players'][name] = {'totalXP':10000+n, 'inventory':{
                        rarity:{f'petal_bench{k}':k+1 for k in range(20)}
                        for rarity in ('common','unusual','rare','epic','legendary','mythic')}}
                (home / 'authority.json').write_text(json.dumps(fixture, separators=(',',':')))
            env = dict(os.environ, SHARD_TEST_MODE='1',
                       SHARD_TEST_STORE=str(home / 'authority.json'),
                       FLIX_AUTHORITY_MODULE=str(root / 'scripts/fixtures/shard-authority-fixture.cjs'))
            log = open(home / 'server.log', 'w+')
            logs.append(log)
            process = subprocess.Popen([node, '-r', './scripts/deploy-runtime.cjs',
                                        'dist/server.js', '--port', str(port),
                                        '--db', str(home / 'forbidden-local.json'), '--bots', '0'],
                                       cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
            processes.append(process)
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    log.seek(0)
                    raise RuntimeError(log.read()[-4000:])
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/api/health', timeout=1) as response:
                        assert json.load(response)['ok']
                    break
                except OSError:
                    time.sleep(.2)
            else:
                raise RuntimeError('WASM startup timed out')
        for index, port in enumerate((3091, 3092)):
            env = dict(os.environ, PORT=str(port), SMOKE_SERVER_ONLY='1')
            smoke = subprocess.run([node, 'scripts/smoke-auth.js'], cwd=root, env=env,
                                   capture_output=True, text=True, timeout=45)
            if smoke.returncode:
                raise RuntimeError(smoke.stdout + smoke.stderr)
            home = base / str(index)
            data = json.loads((home / 'authority.json').read_text())
            assert len(data['users']) >= 2 and len(data['players']) >= 2
            assert data['sessions']
            assert not (home / 'forbidden-local.json').exists()
            print(f'PASS real WASM {index+1}: guest/register/login/bad-password; authoritative host disk; no local fallback', flush=True)
            measurements = [json.loads(line) for line in (home/'authority.json.metrics').read_text().splitlines()]
            durations = sorted(item['hostCommitMillis'] for item in measurements)
            print(json.dumps({'wasm':index+1, 'syntheticAccounts':accounts,
                              'saveBytes':max(item['bytes'] for item in measurements),
                              'hostCommitMedianMs':statistics.median(durations),
                              'hostCommitMaxMs':max(durations),
                              'scope':'Node host write/fsync/rename/directory-fsync only; excludes C++ serialization and network'}), flush=True)
        home = base / '0'
        before = len(json.loads((home / 'authority.json').read_text())['users'])
        (home / 'authority.json.unknown').write_text('simulate lost commit acknowledgement')
        smoke = subprocess.run([node, 'scripts/smoke-auth.js'], cwd=root,
                               env=dict(os.environ, PORT='3091', SMOKE_SERVER_ONLY='1'),
                               capture_output=True, text=True, timeout=45)
        assert smoke.returncode != 0 and 'FAIL guest auth' in smoke.stderr, smoke.stdout + smoke.stderr
        after = json.loads((home / 'authority.json').read_text())
        assert len(after['users']) == before + 1
        processes[0].wait(timeout=15)
        assert not (home / 'forbidden-local.json').exists()
        print('PASS real WASM: committed-but-unknown outcome withheld Auth success and stopped simulation', flush=True)
        print('NOT TESTED: shared accounts, transfer, H global transactions, unified council, cross-host RPC', flush=True)
    finally:
        for process in processes:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
        for process in processes:
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
