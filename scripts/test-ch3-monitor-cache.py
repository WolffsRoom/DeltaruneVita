"""Replay the real Chapter 3 monitor Draw and test cache invalidation/shader pixels."""
from pathlib import Path
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'local-test/logs/ch3-monitor-cache'
OUT.mkdir(parents=True, exist_ok=True)
vs = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat')
cmd = OUT / 'compile.cmd'
cmd.write_text('@echo off\ncall "' + str(vs) + '" -arch=x64 >nul\n'
               'cl /nologo /TC "' + str(ROOT / 'tests/board_screen_cache_test.c') +
               '" /Fe:cache-test.exe >compile.log 2>&1\n'
               'if errorlevel 1 exit /b 1\ncache-test.exe\n')
subprocess.run(['cmd', '/c', str(cmd)], cwd=OUT, check=True)
results = []
for renderer, env_key, marker, gray in [
    ('legacy-gl', 'BS_TEST_BOARD_SCREEN_REGRESSION', 'BOARD_SCREEN_CACHE=pass', False),
    ('modern-gl', 'BS_TEST_BOARD_SCREEN_REGRESSION', 'BOARD_SCREEN_CACHE=pass', False),
    ('legacy-gl', 'BS_TEST_BOARD_SCREEN_REGRESSION', 'BOARD_SCREEN_CACHE=pass', True),
    ('modern-gl', 'BS_TEST_BOARD_SCREEN_REGRESSION', 'BOARD_SCREEN_CACHE=pass', True),
    ('legacy-gl', 'BS_TEST_LEGACY_SHADER_TARGET', 'LEGACY_SHADER_TARGET=pass', False),
]:
    name = renderer + '-' + env_key + ('-gray' if gray else '')
    env = dict(os.environ)
    for key in ('BS_TEST_BOARD_SCREEN_REGRESSION', 'BS_TEST_BOARD_SCREEN_CACHE', 'BS_TEST_LEGACY_SHADER_TARGET'):
        env.pop(key, None)
    env[env_key] = '1'
    env.pop('BS_TEST_BOARD_GRAY', None)
    if gray:
        env['BS_TEST_BOARD_GRAY'] = '1'
    args = [str(ROOT / 'local-test/modern-build/butterscotch.exe'),
            str(ROOT / 'data/prepared/deltarune/deltarunevita/chapter3/data.win'),
            '--renderer', renderer, '--headless', '--lazy-rooms', '--lazy-textures',
            '--start-room', 'room_dw_puzzlecloset_2', '--exit-at-frame', '20',
            '--save-folder', str(OUT / 'save' / name)]
    logfile = OUT / (name + '.log')
    with logfile.open('w') as log:
        code = subprocess.run(args, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=60).returncode
    output = logfile.read_text(errors='replace')
    result = dict(case=name, exit=code, passed=code == 0 and marker in output,
                  measurements=[s for s in output.splitlines() if s.startswith(('BOARD_SCREEN_', 'LEGACY_SHADER_TARGET='))])
    results.append(result)
    print(json.dumps(result), flush=True)
(OUT / 'results.json').write_text(json.dumps(results, indent=2))
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
