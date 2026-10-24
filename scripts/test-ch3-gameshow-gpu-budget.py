from pathlib import Path


root = Path(__file__).resolve().parents[1]
rules_h = (root / "src/vita-probe/source/vita_gpu_rules.h").read_text(encoding="utf-8")
rules_c = (root / "src/vita-probe/source/vita_gpu_rules.c").read_text(encoding="utf-8")
renderer = (root / "src/butterscotch/src/gl/gl_renderer.c").read_text(encoding="utf-8")
legacy_renderer = (root / "src/butterscotch/src/gl_legacy/gl_legacy_renderer.c").read_text(encoding="utf-8")
vm_builtins = (root / "src/butterscotch/src/vm_builtins.c").read_text(encoding="utf-8")
main = (root / "src/vita-probe/source/playable_main.c").read_text(encoding="utf-8")
settings_h = (root / "src/vita-probe/source/vita_settings.h").read_text(encoding="utf-8")
settings_c = (root / "src/vita-probe/source/vita_settings.c").read_text(encoding="utf-8")

# Automatic is the safe default. It selects Legacy before renderer creation
# for Chapter 3, where repeated Vita captures prove that Round 2 crashes even
# under the Modern compatibility submission path. Other chapters keep Modern.
assert "VITA_RENDERER_AUTOMATIC = 0" in settings_h
assert "s->rendererMode = VITA_RENDERER_AUTOMATIC" in settings_c
assert 's->modernGlEnabled = g_activeChapter != 3;' in settings_c
assert '"renderer=modern-gl"' in settings_c
assert '"renderer=legacy-gl"' in settings_c
assert '"automatic"' in settings_c
assert 'renderer_selection_version=1' in settings_c
assert 'strstr(text, "renderer_selection_version=1") == NULL' in settings_c
assert "RENDERER_AUTO_SELECT" in main
assert main.index("VitaSettings_forceLegacyRenderer(&settings)") < main.index("if (settings.modernGlEnabled)")

# Automatic Chapter 3 runs on the stable Legacy backend, but its visible 2D
# effects still compile on demand there. video_yuv remains native-only.
for shader in (
    "shd_channel_change", "shd_channel_change_alt", "shd_channel_change_sprite",
    "shd_chromakey", "shd_grayscale", "shd_grayscale_pal_swapper",
    "shd_grayscalesand", "shd_grayscalesand_tolerance", "shd_hue_rotation",
    "shd_invert", "shd_luminosity_to_transparency", "shd_rainbow",
    "shd_rgb_hsv_shift", "shd_rgb_to_hsv", "shd_rgb_yiq_shift", "shd_tone",
):
    assert f'"{shader}"' in legacy_renderer
assert '"shd_crt"' not in legacy_renderer[legacy_renderer.index("static bool legacyCompactShaderSupported"):legacy_renderer.index("static void glCompileAllShaders")]
assert '"shd_video_yuv"' not in legacy_renderer
assert "glCompileShaderOnDemand" in legacy_renderer

# Keep Modern-GL (and Chapter 3 shaders) active outside the minigame. The
# previous one-quad "Legacy-like" submission profile still crashed on Vita and
# expanded ~400 sprites into ~400 GPU submissions. Round 2 must retain normal
# batching; safety continues through default shaders, RGBA uploads and fenced
# frame-boundary eviction.
assert "VitaGpuRules_ch3LegacySubmissionMode" in rules_h
assert "VitaGpuRules_ch3LegacySubmissionMode" in rules_c
assert "MODERN_GL_CH3_LEGACY_SUBMISSION" not in renderer
assert "if (vitaCh3LegacySubmissionMode(gl)) flushBatch(gl);" not in renderer
assert "if (vitaCh3LegacySubmissionMode(gl)) glFinish();" not in renderer

# The monitor cache now skips its initial copy as well as gray/CRT targets.
# Behavioral cadence and invalidation are covered by board_screen_cache_test.
assert "BoardScreenCache_reuse" in vm_builtins
assert "boardGrayRegionOutsideTarget" in vm_builtins
assert "CH3_BOARD_SCREEN_REUSE=15hz" not in vm_builtins

# Atlas eviction requested by a Draw must be scheduled for the beginning of a
# later frame. Deleting and regenerating a GL name inside the active command
# list is unsafe on vitaGL even when the caller issues glFinish().
assert "VitaGpuRules_deferTextureEvictionToFrameBoundary" in rules_h
assert "VitaGpuRules_deferTextureEvictionToFrameBoundary" in rules_c
assert "MODERN_GL_DEFERRED_EVICT_REQUEST" in renderer
assert "MODERN_GL_DEFERRED_EVICT_RUN" in renderer

# The real-Vita Round 2 trace ends in room_ch3_gameshowroom after the tracked
# atlas set reaches 44.7 MiB. Frame-boundary maintenance now makes the regular
# 48 MiB ceiling safe; a 36 MiB cap is smaller than the measured live set and
# causes pages 8 and 89 to replace each other every frame.
symbol = "VitaGpuRules_modernTextureCacheLimitBytes"
assert symbol in rules_h
assert symbol in rules_c
assert 'strcmp(roomName, "room_ch3_gameshowroom") == 0' in rules_c
assert "48ULL * 1024ULL * 1024ULL" in rules_c
assert "36ULL * 1024ULL * 1024ULL" not in rules_c
assert symbol in renderer

# Preserve the complete Round 2 handoff set captured by the Vita while still
# allowing unrelated gameshow atlases to leave under the regular cache ceiling.
assert "VitaGpuRules_ch3Round2ResidentPage" in rules_h
for page in (1, 8, 34, 64, 89, 94, 101, 103, 105, 133, 211, 250, 251, 253, 337):
    assert str(page) in rules_c
assert "vitaChapter3Round2Staging" in renderer
assert "protectCh3Round2 && VitaGpuRules_ch3Round2ResidentPage(i)" in renderer
assert "VitaGpuRules_modernBc3RoomSafe" in rules_h
assert 'strcmp(roomName, "room_ch3_gameshowroom") == 0' in rules_c
assert 'strncmp(roomName, "room_board_", 11) != 0' in rules_c
assert "VitaGpuRules_modernBc3RoomSafe(g_vitaActiveChapter, roomName)" in renderer
assert "VitaGpuRules_modernShaderSafe" in rules_h
assert "MODERN_GL_CH3_SHADER_FALLBACK shader=%s room=%s" in renderer
assert "!gl->textureMaintenanceActive &&\n            VitaGpuRules_textureRecentlyUsed" not in renderer

# Failed atlas substitutions must stay removed. The compatibility fallback
# also makes the expensive Round 2 prewarm unnecessary on Vita.
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
assert "VitaGpuRules_ch3Round2SafeTexturePage" not in rules_h
assert "VitaGpuRules_ch3Round2SafeTexturePage" not in rules_c
assert "MODERN_GL_CH3_PAGE_REMAP" not in renderer
assert "vitaCh3Round2SafeSubimage" not in vm_builtins
assert "MODERN_GL_CH3_ROUND2_PREWARM" not in runner

print("Chapter 3 gameshow GPU budget contracts: PASS")
