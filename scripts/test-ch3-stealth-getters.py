"""Differential replay of the TV room; host timings are not Vita FPS."""
import json
import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--room',default='room_dw_teevie_stealth_d')
parser.add_argument('--data',type=Path,default=ROOT / 'data/prepared/deltarune/deltarunevita/chapter3/data.win')
parser.add_argument('--output',type=Path,default=ROOT / 'local-test/logs/ch3-stealth-getters')
parser.add_argument('--sample-profiler',action='store_true')
options=parser.parse_args()
OUT = options.output
OUT.mkdir(parents=True, exist_ok=True)
results = []
for renderer in ('legacy-gl', 'modern-gl'):
    for mode in ('baseline', 'optimized', 'regression'):
        name = renderer + '-' + mode
        env = dict(os.environ)
        for key in ('BS_TEST_DISABLE_SELF_GETTERS', 'BS_TEST_SELF_GETTERS', 'BS_TEST_DISABLE_TV_CULL'):
            env.pop(key, None)
        env['BS_TEST_STEALTH_METRICS'] = '1'
        env['BS_TEST_STEALTH_CAMERA'] = '1'
        env.pop('BS_TEST_VITA_PROFILING',None)
        if options.sample_profiler:
            env['BS_TEST_VITA_PROFILING']='1'
        if mode == 'baseline':
            env['BS_TEST_DISABLE_SELF_GETTERS'] = '1'
            env['BS_TEST_DISABLE_TV_CULL'] = '1'
        if mode == 'regression':
            env['BS_TEST_SELF_GETTERS'] = '1'
        args = [str(ROOT / 'local-test/modern-build/butterscotch.exe'),
                str(options.data),
                '--renderer', renderer, '--headless', '--lazy-rooms', '--lazy-textures',
                '--start-room', options.room, '--exit-at-frame', '300',
                '--seed', '42', '--trace-frames', '--save-folder', str(OUT / 'save' / name),
                '--screenshot', str(OUT / (name + '-%d.png')),
                '--screenshot-at-frame', '60', '--screenshot-at-frame', '299']
        frames = (10, 40, 90, 140, 190, 240, 270)
        for frame in frames:
            args += ['--screenshot-at-frame', str(frame)]
        with (OUT / (name + '.log')).open('w') as log:
            code = subprocess.run(args, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90).returncode
        text = (OUT / (name + '.log')).read_text(errors='replace')
        times = [float(ms) for frame, ms in re.findall(r'Frame (\d+) \(End, ([\d.]+) ms\)', text) if int(frame) >= 60]
        passed = code == 0 and ('SELF_GETTER=pass' if mode == 'regression' else 'Exiting at frame 300') in text
        result = dict(case=name, passed=passed, median_host_ms=statistics.median(times) if times else None,
                      regression=[line for line in text.splitlines() if line.startswith(('SELF_GETTER=', 'STEALTH_METRICS'))])
        if mode == 'optimized':
            metrics = re.search(r'STEALTH_METRICS getter_calls=(\d+) culled_panels=(\d+)', text)
            result['optimization_exercised'] = bool(metrics and int(metrics[1]) > 0 and int(metrics[2]) > 0)
            result['identical_pixels'] = all(ImageChops.difference(
                Image.open(OUT / f'{renderer}-baseline-{frame}.png').convert('RGB'),
                Image.open(OUT / f'{renderer}-optimized-{frame}.png').convert('RGB')).getbbox() is None
                for frame in (*frames, 60, 299))
            result['passed'] &= result['identical_pixels'] and result['optimization_exercised']
        results.append(result)
        print(json.dumps(result), flush=True)
(OUT / 'results.json').write_text(json.dumps(results, indent=2))
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
