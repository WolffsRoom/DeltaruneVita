from pathlib import Path

root = Path(__file__).resolve().parents[1]
modern = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
legacy = (root / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")

for source in (modern, legacy):
    assert 'strstr(sprite->name, "logo")' in source
    assert 'strstr(sprite->name, "title")' in source

print("Chapter 3 title BC3 contracts: PASS")
