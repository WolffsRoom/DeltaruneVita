"""Validate the shared font-atlas sampling fix and smoke-test every chapter."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT = (ROOT / "src/butterscotch/src/text_utils.h").read_text(encoding="utf-8")
LEGACY = (ROOT / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")
MODERN = (ROOT / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")

assert "TextUtils_glyphUvStart" in TEXT
assert "TextUtils_glyphUvEnd" in TEXT
assert "+ 0.5f" in TEXT and "- 0.5f" in TEXT
for renderer in (LEGACY, MODERN):
    assert renderer.count("TextUtils_glyphUvStart") >= 4
    assert renderer.count("TextUtils_glyphUvEnd") >= 4
assert "repairBattleGlyphBottom" not in LEGACY

exe = ROOT / "local-test/modern-build/butterscotch.exe"
out = ROOT / "local-test/logs/font-glyph-texel-centers"
out.mkdir(parents=True, exist_ok=True)
for chapter in range(1, 6):
    data = ROOT / f"data/prepared/deltarune/deltarunevita/chapter{chapter}/data.win"
    assert data.exists(), data
    assert b"fnt_main" in data.read_bytes(), f"Chapter {chapter} is missing fnt_main"
    for renderer in ("legacy-gl", "modern-gl"):
        log = out / f"chapter{chapter}-{renderer}.log"
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([
                str(exe), str(data), "--renderer", renderer, "--headless",
                "--lazy-rooms", "--lazy-textures", "--start-room", "PLACE_MENU",
                "--exit-at-frame", "12", "--save-folder", str(out / f"save-ch{chapter}")
            ], cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, log.read_text(errors="replace")[-3000:]

print("font glyph texel-centre regression (Chapters 1-5, Legacy/Modern): PASS")
