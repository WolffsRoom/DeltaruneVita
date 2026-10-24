from pathlib import Path


root = Path(__file__).resolve().parents[1]
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")

assert "vitaEnableChapter5CliffSimpleMode" in runner
assert '"obj_parallax_cliffs_moving_layers"' in runner
assert "VitaGpuRules_ch5CliffSimpleMode(roomName)" in runner
assert '"simple"' in runner
assert '"layer_alpha"' in runner
assert "GMLArray_set" in runner
assert "CH5_CLIFF_SIMPLE=enabled" in runner

rules = (root / "src/vita-probe/source/vita_gpu_rules.c").read_text(encoding="utf-8")
simple_start = rules.index("int VitaGpuRules_ch5CliffSimpleMode")
sync_start = rules.index("int VitaGpuRules_modernSynchronousEviction")
simple = rules[simple_start:sync_start]
assert "room_dw_cliff_kawkawdash" not in simple
assert 'strcmp(roomName, "room_dw_cliff_twirlflowerwind") == 0' in simple
assert 'strcmp(roomName, "room_dw_cliff_precipice") == 0' in simple

modern = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
assert "VitaGpuRules_modernSynchronousEviction(g_vitaActiveChapter, room)" in modern
assert "VitaGpuRules_deferTextureEvictionToFrameBoundary" in modern
assert 'room_dw_cliff_twirlflowerwind' in rules
assert "56ULL * 1024ULL * 1024ULL" in rules

print("Chapter 5 cliff performance contracts: PASS")
