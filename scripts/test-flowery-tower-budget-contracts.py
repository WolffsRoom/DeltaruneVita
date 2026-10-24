from pathlib import Path


root = Path(__file__).resolve().parents[1]
rules = (root / "src/vita-probe/source/vita_gpu_rules.c").read_text(encoding="utf-8")
builtins = (root / "src/butterscotch/src/vm_builtins.c").read_text(encoding="utf-8")
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")

# The Vita trace measured 270-397 ms in the tower recomposition frame even
# after the old 10 Hz / half-layer policy. Surface IDs are allocation-order
# dependent, so filtering layers by ID can remove the complete tower. Preserve
# every layer and reduce only the cached front/back recomposition cadence.
assert "return 1;" in rules
assert "surfaceId & 3" not in rules
assert "frame % 8u == 0u" in rules
assert "CH5_FLOWERY_TOWER_REUSE=3_75hz" in builtins

# The front-face approximation must retain full brightness at the cylinder
# centre. The previous mix started at zero there, making the tower body look
# absent while separately drawn flowers remained visible.
assert 'float shade = mix(1.0, 0.58, edge2);' in renderer

# Structural output surfaces must remain lossless; this optimization only
# reduces their recomposition cadence and the redundant depth slices.
assert "width == 240 && height == 720" in rules

# The latest Vita capture showed a pathological upload/evict loop for the two
# 1024x1024 pages continuously sampled by the late battle phase. They must be
# part of the protected Flowery working set so one-off effects are evicted
# instead of forcing a glFinish and BC3 upload on almost every frame.
flowery_pages = renderer.split("static bool vitaChapter5FloweryBattlePage", 1)[1]
flowery_pages = flowery_pages.split("static bool vitaEvictTexturePage", 1)[0]
assert "0, 56, 71, 89, 104" in flowery_pages

# A font/effect upload must not evict an atlas that the battle sampled in the
# recent-frame safety window. The v0.76-4 trace otherwise moved the eviction
# loop from pages 56/71 to page 89 and paid a synchronous BC3 upload each frame.
eviction = renderer.split("static bool vitaEvictTexturePage", 1)[1]
eviction = eviction.split("static void vitaRequestDeferredTextureMaintenance", 1)[0]
assert eviction.count("(!incomingFont || protectFloweryBattle)") == 2

print("flowery tower budget contracts: PASS")
