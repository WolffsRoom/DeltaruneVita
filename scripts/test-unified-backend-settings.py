from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src/vita-probe/source/vita_settings.h").read_text(encoding="utf-8")
vitagl = (ROOT / "src/vita-probe/source/vita_settings.c").read_text(encoding="utf-8")
gxm = (ROOT / "experiments/native-gxm/vita_settings_native.c").read_text(encoding="utf-8")
launcher = (ROOT / "src/vita-launcher/main.c").read_text(encoding="utf-8")
gxm_main = (ROOT / "experiments/native-gxm/main.c").read_text(encoding="utf-8")
rgba = (ROOT / "experiments/native-gxm/rgba4444_native.h").read_text(encoding="utf-8")
vita_cmake = (ROOT / "src/vita-probe/CMakeLists.txt").read_text(encoding="utf-8")
gxm_cmake = (ROOT / "experiments/native-gxm/CMakeLists.txt").read_text(encoding="utf-8")
audio = (ROOT / "src/butterscotch/src/audio/openal/al_audio_system.c").read_text(encoding="utf-8")

for source in (header, vitagl, gxm):
    assert "graphicsBackend" in source
    assert "pendingGraphicsBackend" in source

for source in (vitagl, gxm):
    assert "VITA_BACKEND_PREFERENCE_PATH" in source
    assert "VitaBackendSelection_save" in source
    assert '"Graphics API"' in source
    assert '"VitaGL"' in source
    assert '"Native-GXM"' in source

assert "VITA_LAUNCHER_COUNTDOWN_US" not in launcher
assert "countdownHeld" in launcher  # L+R remains a recovery selector.

assert "layout=0;" in rgba
assert "g_vitaActiveChapter==0" not in rgba

assert "native_first_load_video_play" in gxm_main
assert 'VitaVideo_open("app0:first_load.mp4")' in gxm_main
assert "VitaVideo_image()" in gxm_main
assert "gxmSwapBuffer()" in gxm_main

assert "../vita-common/vita_backend_selection.c" in vita_cmake
assert "../../src/vita-common/vita_backend_selection.c" in gxm_cmake

shared_music = '"ux0:data/deltarune/deltarunevita/music/%s"'
german_music = '"ux0:data/deltarune/deltarunevita/mods/Lang/German/music/%s"'
assert audio.index(shared_music) < audio.index(german_music)

print("unified backend settings, GXM color and startup video contracts: PASS")
