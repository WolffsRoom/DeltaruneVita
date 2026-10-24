from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src/vita-probe/source/vita_borders.c").read_text(encoding="utf-8")

assert "static int borderBlackHold = 0;" in source
assert "startBorderBlackFade" in source
assert "startImmediateBlackHold" in source
assert 'strcmp(roomName, "room_dw_castle_area_1") == 0' in source
assert 'borderLog("black_hold_immediate")' in source
assert "borderOverlayAlpha = 1.0f;" in source
assert 'strncmp(roomName, "room_gameover", 13) == 0' in source
assert 'strcmp(roomName, "PLACE_FAILURE") == 0' in source
assert 'borderLog("black_fade_begin")' in source
assert 'borderLog("black_hold_complete")' in source
assert 'borderLog("black_hold_released")' in source
assert "borderOverlayTarget = 1.0f;" in source
assert "borderOverlayColor = 0x000000U;" in source
assert "if (borderBlackHold && nextPath[0] != '\\0')" in source
assert "VitaBorders_draw(960, 544);" in source, (
    "launcher return must visibly render the fade before process restart"
)
assert "vglSwapBuffers(GL_FALSE);" in source

runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
header = (root / "src/vita-probe/source/vita_borders.h").read_text(encoding="utf-8")
assert "VitaBorders_noteTransitionObject" in header
assert 'strcmp(objectName, "obj_dw_transition") == 0' in source
assert 'borderLog("transition_black_fade")' in source
assert "VitaBorders_noteTransitionObject(drawObjectName);" in runner

print("border black fade/hold contracts: PASS")
