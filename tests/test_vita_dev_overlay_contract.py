from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = (ROOT / 'src/vita-probe/source/vita_settings.c').read_text(encoding='utf-8')
MAIN = (ROOT / 'src/vita-probe/source/playable_main.c').read_text(encoding='utf-8')


def test_devmode_subtools_are_session_only():
    assert 's->devRoomNavEnabled = strstr(text, "room_nav=1") != NULL;' not in SETTINGS
    assert 's->debugCollisionMasks = strstr(text, "collision_masks=1") != NULL;' not in SETTINGS
    load_start = SETTINGS.index('void VitaSettings_load(VitaSettings* s)')
    load_end = SETTINGS.index('void VitaSettings_applyAudio', load_start)
    load_body = SETTINGS[load_start:load_end]
    assert 's->devRoomNavEnabled = false;' in load_body
    assert 's->debugDevEnabled = false;' in load_body
    assert 's->debugCollisionMasks = false;' in load_body


def test_port_overlays_neutralize_modern_gl_shader():
    start = MAIN.index('Runner_drawGUI(runner, 960, 544, game_w, game_h);')
    end = MAIN.index('if (runner->pendingRoom == -1)', start)
    overlay_pass = MAIN[start:end]
    assert 'renderer->vtable->gpuResetShader(renderer);' in overlay_pass
    assert 'VitaSettings_drawDevOverlay(&settings, renderer,' in overlay_pass
    assert 'renderer->vtable->gpuSetShader(renderer, portOverlaySavedShader);' in overlay_pass
