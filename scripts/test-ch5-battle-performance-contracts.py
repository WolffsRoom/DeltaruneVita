from pathlib import Path

root = Path(__file__).resolve().parents[1]
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
builtins = (root / "src/butterscotch/src/vm_builtins.c").read_text(encoding="utf-8")
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
gpu_rules = (root / "src/vita-probe/source/vita_gpu_rules.c").read_text(encoding="utf-8")

# obj_mane's original Draw creates, samples and frees a 64x64 surface every
# frame. The Vita path must replace that exact event with batched ring geometry.
assert "VMBuiltins_tryManeDraw" in runner
assert '"gml_Object_obj_mane_Draw_0"' in builtins
assert "drawTriangle" in builtins
assert '"targx"' in builtins and '"targy"' in builtins

# The Flowery fight repeatedly uses these pages. They must survive the room's
# working-set pressure, and the room receives a bounded cache allowance.
assert "vitaChapter5FloweryBattlePage" in renderer
for page in (0, 104, 106, 108, 109, 110, 111, 112, 113, 114, 118, 122):
    assert str(page) in renderer
assert "56ULL * 1024ULL * 1024ULL" in gpu_rules
assert "12ULL * 1024ULL * 1024ULL" in renderer

print("Chapter 5 battle performance contracts: PASS")
