"""Validate the Chapter 5 shadow shader against its real data.win."""
import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / "local-test/logs/ch5-shadow"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, BS_TEST_CH5_SHADOW="1")
log = out / "legacy-gl.log"
with log.open("w") as stream:
    result = subprocess.run([
        str(root / "local-test/modern-build/butterscotch.exe"),
        str(root / "data/prepared/deltarune/deltarunevita/chapter5/data.win"),
        "--renderer", "legacy-gl", "--headless", "--lazy-rooms", "--lazy-textures",
        "--start-room", "room_town_south", "--exit-at-frame", "12",
        "--save-folder", str(out / "save")
    ], cwd=root, env=env, stdout=stream, stderr=subprocess.STDOUT, timeout=90)
text = log.read_text(errors="replace")
assert result.returncode == 0 and "CH5_SHADOW=pass" in text, text[-3000:]
print("chapter 5 shadow shader: PASS")
