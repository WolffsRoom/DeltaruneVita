from pathlib import Path

root = Path(__file__).resolve().parents[1]
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")

assert 'strncmp(sprite->name, "spr_blocktree_", 14) != 0' in renderer, (
    "Modern BC3 policy must keep animated blocktree component pages lossless"
)
assert 'strcmp(objectName, "obj_castlereflect_manager") == 0' in runner, (
    "Chapter 5 final-battle reflection manager must have a Vita update throttle"
)
assert 'eventSubtype == DRAW_BEGIN' in runner, (
    "Only the reflection surface rebuild Draw Begin event may be throttled"
)
assert 'Renderer_surfaceExists(runner->renderer, (int32_t)RValue_toReal(surf))' in runner, (
    "The first reflection frame must build the surface before reuse is allowed"
)
assert '(runner->frameCount % 3U) != 0U' in runner, (
    "Reflection surface should update at 10 Hz on the 30 FPS Vita path"
)
assert 'CH5_CASTLEREFLECT_REUSE=10hz' in runner, (
    "Hardware logs must identify the active reflection reuse optimization"
)

print("Chapter 5 final reflection and blocktree contracts: PASS")
