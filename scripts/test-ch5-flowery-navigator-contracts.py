from pathlib import Path


root = Path(__file__).resolve().parents[1]
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
legacy = (root / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")
frontend = (root / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")

reflection = runner.split('strcmp(objectName, "obj_castlereflect_manager") == 0', 1)[0]
reflection = reflection.rsplit("if (g_vitaActiveChapter == 5", 1)[1]
assert "g_vitaModernGlActive" not in reflection
assert "CH5_CASTLEREFLECT_REUSE=10hz backend=all" in runner

headroom = legacy.split("static bool vitaTextureUploadHasPhysicalHeadroom", 1)[1]
headroom = headroom.split("if (vitaChapter3TvMemoryMode)", 1)[0]
assert "8ULL * 1024ULL * 1024ULL" in headroom

jump = frontend.split("DEV_ROOM_JUMP_GPU_BARRIER=complete", 1)[0]
jump = jump.rsplit("if (confirmRoom)", 1)[1]
assert "renderer->vtable->flush(renderer);" in jump
assert "glFinish();" in jump
assert "GLLegacyRenderer_collectRetiredTexturesForRoomChange" in jump
assert "vglForceGarbageCollection();" in jump

print("ch5 flowery/navigator contracts: PASS")
