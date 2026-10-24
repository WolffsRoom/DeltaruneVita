#include "vita_gpu_rules.h"

#include <string.h>

int VitaGpuRules_retiredTextureMature(uint32_t currentFrame, uint32_t retiredFrame) {
    return (uint32_t)(currentFrame - retiredFrame) >= 4u;
}

int VitaGpuRules_textureRecentlyUsed(uint32_t currentFrame, uint32_t lastUsedFrame) {
    return (uint32_t)(currentFrame - lastUsedFrame) < 8u;
}

int VitaGpuRules_deferTextureEvictionToFrameBoundary(int chapter, const char* roomName) {
    if (roomName == NULL) return 0;
    if (chapter == 3)
        return strcmp(roomName, "room_ch3_gameshowroom") == 0 ||
               strncmp(roomName, "room_board_", 11) == 0;
    if (chapter == 5)
        return strcmp(roomName, "room_dw_cliff_kawkawdash") == 0 ||
               strcmp(roomName, "room_dw_cliff_twirlflowerwind") == 0 ||
               strcmp(roomName, "room_dw_cliff_precipice") == 0;
    return 0;
}

int VitaGpuRules_ch5CliffSimpleMode(const char* roomName) {
    if (roomName == NULL) return 0;
    // Kawkawdash composes its moving hazards with the complete cliff draw
    // path. Hiding the secondary layers also hides the enemy attack there.
    return strcmp(roomName, "room_dw_cliff_twirlflowerwind") == 0 ||
           strcmp(roomName, "room_dw_cliff_precipice") == 0;
}

int VitaGpuRules_modernSynchronousEviction(int chapter, const char* roomName) {
    (void)chapter;
    (void)roomName;
    // Kept as an API boundary for older call sites. Eviction inside an active
    // Draw is unsafe even with glFinish on Vita. Crash-prone rooms now request
    // maintenance and perform the synchronized deletion at the next frame
    // boundary instead.
    return 0;
}

int VitaGpuRules_ch3Round2ResidentPage(uint32_t pageId) {
    // Captured by the GPU dump at the Round 2 handoff. Page 105 was loaded,
    // evicted and loaded again before room_board_2 existed, so protecting only
    // the destination room is too late.
    static const uint16_t pages[] = {
        1, 8, 34, 64, 89, 94, 101, 103, 105, 133, 211, 250, 251, 253, 337
    };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i)
        if (pages[i] == pageId) return 1;
    return 0;
}

int VitaGpuRules_ch3LegacySubmissionMode(int chapter, const char* roomName) {
    (void)chapter;
    (void)roomName;
    // Real Vita v0.76-9 still GPU-crashed in gameshowroom after a long run.
    // The per-quad submission profile expanded roughly 400 sprites into 400
    // command submissions and did not reproduce Legacy's internal lifetime.
    // Keep this compatibility API disabled; the proven safeguards are the
    // default-shader fallback, RGBA texture path and frame-boundary eviction.
    return 0;
}

int VitaGpuRules_modernBc3RoomSafe(int chapter, const char* roomName) {
    if (chapter != 3 || roomName == NULL) return 1;
    // The fixed-function renderer can sample these compressed atlases, but
    // real-Vita GPU dumps repeatedly stop while the programmable renderer is
    // staging Round 2. Keep Modern-GL and its shaders active while using the
    // proven RGBA4444 upload path for this minigame only.
    return strcmp(roomName, "room_ch3_gameshowroom") != 0 &&
           strncmp(roomName, "room_board_", 11) != 0;
}

int VitaGpuRules_modernShaderSafe(int chapter, const char* roomName,
                                  const char* shaderName) {
    if (chapter != 3 || roomName == NULL || shaderName == NULL) return 1;
    const int round2Room = strcmp(roomName, "room_ch3_gameshowroom") == 0 ||
                           strncmp(roomName, "room_board_", 11) == 0;
    // The fixed-function renderer completes Round 2. In the Modern backend,
    // use its basic textured program for every shader request in the affected
    // rooms; Chapter 3 shaders remain available everywhere else.
    return !round2Room;
}

uint64_t VitaGpuRules_modernTextureCacheLimitBytes(int chapter, const char* roomName,
                                                   int floweryBattleRoom) {
    if (floweryBattleRoom) return 56ULL * 1024ULL * 1024ULL;
    if (chapter == 5 && roomName != NULL &&
        (strcmp(roomName, "room_dw_cliff_kawkawdash") == 0 ||
         strcmp(roomName, "room_dw_cliff_twirlflowerwind") == 0 ||
         strcmp(roomName, "room_dw_cliff_precipice") == 0))
        return 56ULL * 1024ULL * 1024ULL;
    // Round 2 uses a measured live set larger than 36 MiB. Its cache cleanup is
    // fenced at frame boundaries, so it can use the regular ceiling without
    // forcing the screen atlas (page 8) and board atlas (page 89) to replace
    // each other every frame.
    return 48ULL * 1024ULL * 1024ULL;
}

int VitaGpuRules_floweryStructuralSurface(int width, int height) {
    return width == 240 && height == 720;
}

int VitaGpuRules_floweryTowerLayerVisible(int surfaceId, int sourceWidth, int sourceHeight,
                                          int targetWidth, int targetHeight) {
    (void)surfaceId;
    (void)sourceWidth;
    (void)sourceHeight;
    (void)targetWidth;
    (void)targetHeight;
    // Surface IDs depend on allocation order. Filtering depth slices by their
    // numeric ID can therefore discard every visible slice. Preserve the full
    // tower and reduce cost only through the cached recomposition cadence.
    return 1;
}

int VitaGpuRules_floweryTowerUpdateFrame(uint32_t frame) {
    // The tower rotates slowly and is cached in front/back surfaces. Updating
    // those surfaces at 3.75 Hz preserves the slow movement while reducing the
    // GPU backlog that kept the otherwise static room near 22 FPS.
    return frame % 8u == 0u;
}
