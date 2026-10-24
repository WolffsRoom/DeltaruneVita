"""Regression of GL blend-state restoration and transformed sprite visibility."""
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'local-test/logs/finale-render'
out.mkdir(parents=True,exist_ok=True)
for renderer in ('legacy-gl','modern-gl'):
    with (out/(renderer+'.log')).open('w') as log:
        result=subprocess.run([str(root/'local-test/modern-build/butterscotch.exe'),
            str(root/'data/prepared/deltarune/deltarunevita/chapter3/data.win'),
            '--renderer',renderer,'--headless','--lazy-rooms','--lazy-textures',
            '--start-room','room_town_shelter','--exit-at-frame','12',
            '--save-folder',str(out/('save-'+renderer))],cwd=root,
            env=dict(os.environ,BS_TEST_FINALE_RENDER='1'),stdout=log,stderr=subprocess.STDOUT,timeout=90)
    text=(out/(renderer+'.log')).read_text(errors='replace')
    assert result.returncode==0 and all(mark+'=pass' in text for mark in
        ('FINALE_BLEND','FINALE_MASK','FINALE_PROJECTION')),renderer
    print(renderer,'PASS')
