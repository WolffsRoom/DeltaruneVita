from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
borders = (root / "src/vita-probe/source/vita_borders.c").read_text(encoding="utf-8")
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")

assert '"spr_spin_petal_particlefps"' in main
assert "int spriteIndex = 6114;" not in main
assert "fallbackIndex = 6127" in main
assert "spinPetal->spriteIndex" not in main
assert "borderTransitionStart = startGardenReveal ? 0" in borders
for page in (121, 124, 701, 710, 726):
    assert str(page) in renderer
assert "tensionDrawFrame" not in runner
print("chapter5 Garden transition and TP page contracts: PASS")
