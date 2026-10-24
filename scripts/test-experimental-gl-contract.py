from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SETTINGS_H = (ROOT / "src/vita-probe/source/vita_settings.h").read_text(encoding="utf-8")
SETTINGS_C = (ROOT / "src/vita-probe/source/vita_settings.c").read_text(encoding="utf-8")
MAIN_C = (ROOT / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
RUNNER_C = (ROOT / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
LEGACY_H = (ROOT / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.h").read_text(encoding="utf-8")
LEGACY_C = (ROOT / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")

require_gpu_guard = [
    '"castle_palette_gpu_budget"',
    'strcmp(activeShaderName, "shd_pal_swapper") == 0',
    'strncmp(activeRoomName, "room_dw_castle_"',
]


def require(text: str, source: str, message: str) -> None:
    assert text in source, message


for guard in require_gpu_guard:
    require(guard, LEGACY_C, "Castle Town palette GPU guard is missing")


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

# Legacy's compact bridge is the only programmable path used by the new mode.
# Its policy and diagnostics must stay explicit and bounded.
require("GLLegacyRenderer_experimentalShadersEnabled", LEGACY_H,
        "Legacy renderer must expose one Experimental shader policy boundary")
require("g_vitaExperimentalGlActive && g_vitaShadersEnabled", LEGACY_C,
        "Experimental shader policy must combine renderer mode and toggle")
for shader_name in ("shd_channel_change", "shd_prophecy", "shd_fountaineffect",
                    "shd_shadowblend", "shd_shoujo"):
    require(f'"{shader_name}"', LEGACY_C,
            f"audited shader missing from compact allow-list: {shader_name}")
assert '"shd_video_yuv"' not in LEGACY_C, "native multi-plane video shader must remain excluded"
require("gl->shaderAttempted[shaderIndex] = true;", LEGACY_C,
        "shader attempts must be cached to prevent per-frame retries")
require("EXPERIMENTAL_SHADER=%s", LEGACY_C,
        "experimental diagnostics must use the documented outcome field")
require('vitaLogExperimentalShader(gl, "compiled"', LEGACY_C,
        "successful experimental compilation must be logged")
require('vitaLogExperimentalShader(gl, "fallback"', LEGACY_C,
        "unsupported or failed shaders must log a bounded fallback")
require('vitaLogExperimentalShader(gl, "active"', LEGACY_C,
        "experimental activation must be logged on state transition")

# Town shadows are common, tiny shaders.  Keep dedicated Vita programs instead
# of feeding the generated GameMaker fragment preamble to ShaRK; that path may
# report a linked program while producing no shadow pixels on hardware.
require("shadowFragmentVita", LEGACY_C,
        "Experimental-GL needs a compact Vita fragment for town shadows")
require("shadowEveningFragmentVita", LEGACY_C,
        "Experimental-GL needs the evening shadow edge program")
require('strcmp(shaderName, "shd_shadowblend") == 0', LEGACY_C,
        "the compact town shadow program must be selected by resource name")
require('strcmp(shaderName, "shd_shadowblend_evening") == 0', LEGACY_C,
        "the compact evening shadow program must be selected by resource name")
require("uniform vec4 shadowCol", LEGACY_C,
        "town shadow colour/alpha contract must be preserved")
require("uniform vec2 pixelw", LEGACY_C,
        "evening shadow edge contract must be preserved")
require("vitaCustomShaderActive", LEGACY_C,
        "Vita draw bridge must distinguish programmable shaders from FFP")
require("glEnableVertexAttribArray(0)", LEGACY_C,
        "Experimental shaders must receive the position attribute")
require("glVertexAttribPointer(1, 2", LEGACY_C,
        "Experimental shaders must receive texture coordinates")
require("glVertexAttribPointer(2, 4", LEGACY_C,
        "Experimental shaders must receive vertex colour")
require("vitaCustomShaderActive = true;", LEGACY_C,
        "activating a compiled shader must switch the Vita vertex bridge")
require("vitaCustomShaderActive = false;", LEGACY_C,
        "shader reset/fallback must restore the Legacy vertex bridge")

print("Experimental-GL settings contract: PASS")
