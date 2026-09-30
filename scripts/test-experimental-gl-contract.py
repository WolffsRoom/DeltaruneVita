from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETTINGS_H = (ROOT / "src/vita-probe/source/vita_settings.h").read_text(encoding="utf-8")
SETTINGS_C = (ROOT / "src/vita-probe/source/vita_settings.c").read_text(encoding="utf-8")
MAIN_C = (ROOT / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
RUNNER_C = (ROOT / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")


def require(text: str, source: str, message: str) -> None:
    assert text in source, message


# The new value is appended so existing INI numeric state and old builds keep
# the established Automatic/Legacy/Modern mapping.
require("VITA_RENDERER_EXPERIMENTAL = 3", SETTINGS_H,
        "Experimental renderer enum must be appended with value 3")

# Automatic remains the default and old INIs without the selection marker keep
# migrating to it.
require("s->rendererMode = VITA_RENDERER_AUTOMATIC;", SETTINGS_C,
        "Automatic must remain the default renderer")
require('strstr(text, "renderer_selection_version=1") == NULL', SETTINGS_C,
        "old renderer INIs must retain the Automatic migration")

# The new mode must round-trip through config.ini and be visible in the menu.
require('s->rendererMode == VITA_RENDERER_EXPERIMENTAL ? "experimental-gl"', SETTINGS_C,
        "saveSettings must serialize Experimental-GL")
require('strstr(text, "renderer=experimental-gl")', SETTINGS_C,
        "VitaSettings_load must parse Experimental-GL")
require('settingsText(s, "Experimental GL", "Experimental GL", "Experimental GL")', SETTINGS_C,
        "the renderer selector must display Experimental GL")

# Both navigation directions must cover all four entries.
require("(s->pendingRendererMode + 1) % 4", SETTINGS_C,
        "right navigation must cycle four renderer modes")
require("(s->pendingRendererMode + 3) % 4", SETTINGS_C,
        "left navigation must cycle four renderer modes")

# Experimental-GL is a Legacy policy. It must never enter the Modern factory
# branch, even in chapters where Automatic normally selects Modern.
require("bool g_vitaExperimentalGlActive = false;", MAIN_C,
        "runtime must expose a dedicated Experimental-GL policy flag")
require("s->rendererMode == VITA_RENDERER_EXPERIMENTAL", SETTINGS_C,
        "forceLegacyRenderer must recognize Experimental-GL")
require("g_vitaExperimentalGlActive = settings.rendererMode == VITA_RENDERER_EXPERIMENTAL;", MAIN_C,
        "startup must derive the experimental policy from the selected mode")
require('"RENDERER=experimental-gl backend=legacy shaders=%d"', MAIN_C,
        "startup log must identify Experimental-GL as a Legacy backend")
require("extern bool g_vitaExperimentalGlActive;", RUNNER_C,
        "runner-side shader policy must consume the dedicated mode flag")
require("g_vitaExperimentalGlActive && g_vitaShadersEnabled", RUNNER_C,
        "experimental shader work must require both mode and Shaders On")

print("Experimental-GL settings contract: PASS")
