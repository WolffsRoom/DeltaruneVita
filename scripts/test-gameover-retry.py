"""Host regression for failure state guards and save/checkpoint precedence."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'local-test/logs/gameover-retry'
out.mkdir(parents=True, exist_ok=True)
for renderer in ('legacy-gl', 'modern-gl'):
    fixtures = {
        'save/filech3_9': 'live checkpoint', 'mod/filech3_9': 'old checkpoint',
        'save/filech3_0': 'live save', 'mod/filech3_0': 'old save',
        'save/translation.txt': 'save translation', 'mod/translation.txt': 'mod translation',
        'bundle/filech5_9': 'bundled checkpoint',
    }
    for name, value in fixtures.items():
        p = root / 'scratch/retry-fs' / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(value, encoding='utf-8')
    env = dict(os.environ, BS_TEST_RETRY='1')
    with (out / (renderer + '.log')).open('w') as log:
        result = subprocess.run([
            str(root / 'local-test/modern-build/butterscotch.exe'),
            str(root / 'data/prepared/deltarune/deltarunevita/chapter3/data.win'),
            '--renderer', renderer, '--headless', '--lazy-rooms', '--lazy-textures',
            '--start-room', 'room_dw_teevie_stealth_d', '--exit-at-frame', '12',
            '--save-folder', str(out / ('save-' + renderer)),
        ], cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    text = (out / (renderer + '.log')).read_text(errors='replace')
    assert result.returncode == 0 and 'RETRY_REGRESSION=pass' in text, renderer
    print(renderer, 'PASS')
