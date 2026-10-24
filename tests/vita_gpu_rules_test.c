#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../src/vita-probe/source/vita_gpu_rules.h"

int main(void) {
    assert(!VitaGpuRules_retiredTextureMature(100u, 100u));
    assert(!VitaGpuRules_retiredTextureMature(103u, 100u));
    assert(VitaGpuRules_retiredTextureMature(104u, 100u));
    assert(VitaGpuRules_retiredTextureMature(120u, 100u));
    assert(!VitaGpuRules_retiredTextureMature(1u, UINT32_MAX));
    assert(VitaGpuRules_retiredTextureMature(3u, UINT32_MAX));

    // Source layer atlases contain already quantized art and may use RGBA4;
    // only the final 240x720 tower composites need full-color accumulation.
    assert(!VitaGpuRules_floweryStructuralSurface(360, 360));
    assert(VitaGpuRules_floweryStructuralSurface(240, 720));
    assert(!VitaGpuRules_floweryStructuralSurface(640, 480));
    assert(!VitaGpuRules_floweryStructuralSurface(240, 360));
    assert(!VitaGpuRules_floweryStructuralSurface(320, 180));

    // The Vita final battle samples alternate source layers only while
    // composing the expensive 240x720 front/back tower targets.
    assert(VitaGpuRules_floweryTowerLayerVisible(10, 360, 360, 240, 720));
    assert(VitaGpuRules_floweryTowerLayerVisible(11, 360, 360, 240, 720));
    assert(VitaGpuRules_floweryTowerLayerVisible(11, 360, 360, 640, 480));
    assert(VitaGpuRules_floweryTowerLayerVisible(11, 128, 128, 240, 720));

    assert(VitaGpuRules_floweryTowerUpdateFrame(0));
    assert(!VitaGpuRules_floweryTowerUpdateFrame(1));
    assert(!VitaGpuRules_floweryTowerUpdateFrame(2));
    assert(!VitaGpuRules_floweryTowerUpdateFrame(3));
    assert(VitaGpuRules_floweryTowerUpdateFrame(6));

    // Large atlases used by alternating animation frames must not be evicted
    // immediately. That produced a reload/retire loop and eventually a Vita
    // GPU crash in Chapter 5 shops and cliff rooms.
    assert(VitaGpuRules_textureRecentlyUsed(100u, 100u));
    assert(VitaGpuRules_textureRecentlyUsed(107u, 100u));
    assert(!VitaGpuRules_textureRecentlyUsed(108u, 100u));
    assert(VitaGpuRules_textureRecentlyUsed(2u, UINT32_MAX));

    // Modern-GL must never delete/recreate atlas textures from inside a Draw
    // in the Chapter 3 Round 2 pipeline. Maintenance is fenced at the start
    // of the following frame instead.
    assert(VitaGpuRules_deferTextureEvictionToFrameBoundary(3, "room_ch3_gameshowroom"));
    assert(VitaGpuRules_deferTextureEvictionToFrameBoundary(3, "room_board_2"));
    assert(!VitaGpuRules_deferTextureEvictionToFrameBoundary(3, "room_intro"));
    assert(!VitaGpuRules_deferTextureEvictionToFrameBoundary(5, "room_ch3_gameshowroom"));
    assert(VitaGpuRules_deferTextureEvictionToFrameBoundary(
        5, "room_dw_cliff_kawkawdash"));
    assert(VitaGpuRules_deferTextureEvictionToFrameBoundary(
        5, "room_dw_cliff_twirlflowerwind"));
    assert(VitaGpuRules_deferTextureEvictionToFrameBoundary(
        5, "room_dw_cliff_precipice"));
    assert(VitaGpuRules_ch3Round2ResidentPage(8));

    // The Kawkaw hazard uses the complete cliff composition. The lightweight
    // parallax profile is safe only in the neighboring scenery rooms.
    assert(!VitaGpuRules_ch5CliffSimpleMode("room_dw_cliff_kawkawdash"));
    assert(VitaGpuRules_ch5CliffSimpleMode("room_dw_cliff_twirlflowerwind"));
    assert(VitaGpuRules_ch5CliffSimpleMode("room_dw_cliff_precipice"));

    // These rooms reuse alternating atlases after the retirement queue would
    // have released their old GL names. Fence their evictions on Vita.
    assert(!VitaGpuRules_modernSynchronousEviction(
        5, "room_dw_cliff_twirlflowerwind"));
    assert(!VitaGpuRules_modernSynchronousEviction(
        5, "room_dw_cliff_kawkawdash"));
    assert(!VitaGpuRules_modernSynchronousEviction(
        4, "room_dw_cliff_twirlflowerwind"));
    assert(VitaGpuRules_modernTextureCacheLimitBytes(
        5, "room_dw_cliff_twirlflowerwind", 0) == 56ULL * 1024ULL * 1024ULL);
    assert(VitaGpuRules_modernTextureCacheLimitBytes(
        5, "room_dw_cliff_kawkawdash", 0) == 56ULL * 1024ULL * 1024ULL);

    // Real-Vita Round 2 crash dump: these pages enter during the same-room
    // minigame handoff. Page 105 was evicted and requested again immediately.
    const uint32_t round2Pages[] = { 34, 64, 89, 94, 101, 103, 105, 133, 211, 251, 253 };
    for (unsigned i = 0; i < sizeof(round2Pages) / sizeof(round2Pages[0]); ++i)
        assert(VitaGpuRules_ch3Round2ResidentPage(round2Pages[i]));
    assert(!VitaGpuRules_ch3Round2ResidentPage(0));
    assert(!VitaGpuRules_ch3Round2ResidentPage(104));

    // The v0.75-31 GPU dump repeatedly terminates immediately after runtime
    // texture page 63 is loaded. In the active Chapter 3 data this page is
    // unique to frames 0 and 7 of the full-screen technical-difficulties
    // animation. Adjacent frames use separate atlases and preserve the scene.
    assert(VitaGpuRules_ch3Round2ResidentPage(64));
    assert(VitaGpuRules_ch3Round2ResidentPage(101));
    assert(VitaGpuRules_ch3Round2ResidentPage(103));
    assert(VitaGpuRules_ch3Round2ResidentPage(253));

    // BC3 is stable in the fixed-function renderer, but the Round 2 atlases
    // crash inside the Vita driver when sampled by Modern-GL shaders.
    assert(!VitaGpuRules_modernBc3RoomSafe(3, "room_ch3_gameshowroom"));
    assert(!VitaGpuRules_modernBc3RoomSafe(3, "room_board_2"));
    assert(!VitaGpuRules_modernBc3RoomSafe(3, "room_board_3"));
    assert(VitaGpuRules_modernBc3RoomSafe(3, "room_dw_green_room"));
    assert(VitaGpuRules_modernBc3RoomSafe(5, "room_board_2"));
    assert(VitaGpuRules_modernBc3RoomSafe(3, NULL));

    assert(!VitaGpuRules_modernShaderSafe(
        3, "room_ch3_gameshowroom", "shd_crt"));
    assert(!VitaGpuRules_modernShaderSafe(3, "room_board_2", "shd_crt"));
    assert(!VitaGpuRules_modernShaderSafe(
        3, "room_ch3_gameshowroom", "shd_palette"));
    assert(VitaGpuRules_modernShaderSafe(3, "room_dw_green_room", "shd_crt"));
    assert(VitaGpuRules_modernShaderSafe(5, "room_board_2", "shd_crt"));

    assert(VitaGpuRules_ch3LegacySubmissionMode(3, "room_ch3_gameshowroom"));
    assert(VitaGpuRules_ch3LegacySubmissionMode(3, "room_board_2"));
    assert(VitaGpuRules_ch3LegacySubmissionMode(3, "room_board_empty"));
    assert(!VitaGpuRules_ch3LegacySubmissionMode(3, "room_dw_green_room"));
    assert(!VitaGpuRules_ch3LegacySubmissionMode(5, "room_board_2"));

    // Monitor scheduling is exercised by board_screen_cache_test.c and the
    // desktop replay of the real GML Draw event.

    puts("vita_gpu_rules_test: PASS");
    return 0;
}
