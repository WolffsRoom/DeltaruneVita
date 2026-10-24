from pathlib import Path


root = Path(__file__).resolve().parents[1]
legacy = (root / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")

assert "g_vitaActiveChapter == 2 && vitaCh2TexturePolicyRegion == 4" in legacy
assert "return 92ULL * 1024ULL * 1024ULL;" in legacy
assert "bool vitaSharedCh2MansionTransition" in runner
assert "vitaSharedCh2MansionTransition ? 88ULL" in runner
assert "g_vitaActiveChapter == 2 ? vitaSharedCh2MansionTransition" in runner

print("Chapter 2 Mansion GPU budget contracts: PASS")
