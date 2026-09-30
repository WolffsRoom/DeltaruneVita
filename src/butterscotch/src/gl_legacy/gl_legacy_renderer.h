#ifndef _BS_GL_LEGACY_RENDERER_H_
#define _BS_GL_LEGACY_RENDERER_H_

#include "common.h"
#include "renderer.h"
#include "runner.h"
#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#elif defined(PLATFORM_VITA)
#include <vitaGL.h>
// Vita GPU Texture cache configurations
#define VITA_CPU_TEXTURE_CACHE_SLOTS 1
#define VITA_RETIRED_TEXTURE_SLOTS 16
#else
#include <glad/glad.h>
#endif

// ===[ GLLegacyRenderer Struct ]===
// Exposed in the header so platform-specific code (main.c) can access FBO fields for screenshots.
typedef struct {
    Renderer base; // Must be first field for struct embedding

    GLuint* glTextures;       // one GL texture per TXTR page
    GLuint* shaderPrograms;   // compiled GL program per GameMaker shader (0 = fixed pipeline fallback)
    bool* shaderAttempted;    // whether AOT compilation was tried for each shader
    int32_t* textureWidths;   // needed for UV normalization
    int32_t* textureHeights;
    bool* textureLoaded;      // lazy loading: true once PNG decoded and uploaded
    uint32_t* textureLastUsedFrame;
#ifdef PLATFORM_VITA
    // Last frame in which an atlas was evicted.  Kept separately from
    // textureLastUsedFrame because eviction clears the GL residency state.
    // This lets the Vita LRU identify a page that is bouncing in and out of
    // memory and preserve the emerging working set instead of thrashing it.
    uint32_t* textureLastEvictedFrame;
#endif
    bool* texturePinned;
    uint32_t textureFrame;
    // Frame in which a room transition began. Runtime LRU eviction is held
    // briefly so the first destination draw cannot collect textures that are
    // still referenced by the outgoing/incoming GXM command buffers.
    uint32_t textureRoomChangeFrame;
    uint32_t textureCacheBlockedFrame;
    bool textureRoomTrimActive;
    uint64_t residentTextureBytes;
    uint32_t textureCount;
#ifdef PLATFORM_VITA
    // Static atlas manifest for the room being entered. Pages already shared
    // with the outgoing room survive the transition trim; UI/font pages use
    // texturePinned independently.
    bool* textureRoomRequired;
    // Subset of textureRoomRequired that belongs to static room art and is
    // worth uploading during the loading screen. Object default sprites stay
    // protected by textureRoomRequired, but load on first use; eagerly loading
    // every object page made Chapter 5 enter town_south at ~99 MiB VRAM.
    bool* textureRoomPreload;
    // Exact GPU allocation for original atlas pages. Compressed PVR pages do
    // not have the same byte size as their RGBA dimensions.
    uint64_t* textureGpuBytes;
    // VTC4 source CRCs are expensive to verify because validation may map or
    // allocate the original compressed atlas. Once a page is verified against
    // the current DataWin, it remains valid for the lifetime of this runner.
    bool* textureCacheValidated;
    // Frame after which a texture that could not fit may be attempted again.
    // Avoids decoding/reading the same missing atlas on every frame.
    uint32_t* textureRetryAfterFrame;
    uint16_t* textureRetryDelayFrames;
    // Highest camera relevance observed for each atlas in its most recent
    // frame: 0=far, 1=read-ahead margin, 2=visible. LRU uses this to reclaim
    // off-camera pages before visible animation/UI pages.
    uint8_t* textureCameraClass;
    uint32_t* textureCameraClassFrame;
    // Some rooms exceed the Vita pool even with RGBA4444. In those rooms only
    // non-pinned, non-font scenery atlases use the display-aware size profile.
    bool vitaConstrainedRoomTextures;
    // Rooms whose visible atlas set exceeds both the GPU budget and the
    // user-RAM fallback pool. These use a 1024px scenery cap and do not retain
    // a decoded 2048px page in the CPU cache.
    bool vitaSevereRoomTextures;
    int textureBorderState;
    // Keep only the most recently decoded lossless atlas in user RAM. PVR
    // pages upload directly and never enter this cache. A single 2048x2048
    // RGBA4444 page costs 8 MiB; retaining two unnecessarily raised the peak
    // during Chapter 5 room changes.
    uint16_t* cpuTextureCachePixels[VITA_CPU_TEXTURE_CACHE_SLOTS];
    uint32_t cpuTextureCachePage[VITA_CPU_TEXTURE_CACHE_SLOTS];
    uint32_t cpuTextureCacheStamp[VITA_CPU_TEXTURE_CACHE_SLOTS];
    int32_t cpuTextureCacheWidth[VITA_CPU_TEXTURE_CACHE_SLOTS];
    int32_t cpuTextureCacheHeight[VITA_CPU_TEXTURE_CACHE_SLOTS];
    uint32_t cpuTextureCacheClock;
    // Runtime LRU eviction must not immediately reuse a GL texture name while
    // an older Vita GXM command list can still reference it.
    GLuint retiredTextures[VITA_RETIRED_TEXTURE_SLOTS];
    uint32_t retiredTextureFrames[VITA_RETIRED_TEXTURE_SLOTS];
    uint64_t retiredTextureBytes[VITA_RETIRED_TEXTURE_SLOTS];
    uint32_t vitaTextureEvictions;
    uint32_t vitaTextureDeferred;
    uint32_t vitaTextureRamHits;
    uint32_t frameSubmittedPrimitives;
    uint32_t frameFlushCount;
    // At most one atlas that is near (but not yet inside) the active 4:3
    // camera is uploaded per frame. This turns camera movement into gradual
    // read-ahead instead of loading every off-screen Draw event at once.
    uint32_t vitaNearTextureLoadFrame;
    int appliedTextureFilterRevision;
    // One safe, purpose-built legacy shader for Chapter 5's logo. Compiling
    // the complete GameMaker shader table is intentionally avoided on Vita.
    GLuint vitaShoujoProgram;
    GLint vitaShoujoMvp;
    GLint vitaShoujoTime;
    GLint vitaShoujoWidth;
    GLint vitaShoujoGradient;
    GLint vitaShoujoBubble;
    GLint vitaShoujoStars;
    GLuint vitaShadowProgram;
    GLint vitaShadowMvp;
    GLint vitaShadowColor;
#endif

    GLuint whiteTexture; // 1x1 white pixel for drawing primitives (rectangles, lines, etc.)

    int32_t windowW; // stored from beginFrame for endFrame blit
    int32_t windowH;
    int32_t gameW; // game width (matches the application_surface size)
    int32_t gameH; // game height (matches the application_surface size)

    // Original counts from data.win (dynamic slots start at these indices)
    uint32_t originalTexturePageCount;
    uint32_t originalTpagCount;
    uint32_t originalSpriteCount;

    bool colorWriteR, colorWriteG, colorWriteB, colorWriteA;
    bool alphaTestEnable;

    // GML surfaces (each is an FBO with a backing color texture)
    GLuint* surfaces;
    GLuint* surfaceTexture;
    int32_t* surfaceWidth;
    int32_t* surfaceHeight;
    uint32_t surfaceCount;
#ifdef PLATFORM_VITA
    // Chapter 3's circle-zoom darkness effect asks for a room-sized surface.
    // Keep only the camera-sized window resident and move it with the view.
    int32_t vitaCouchEffectSurface;
    float vitaCouchEffectX;
    float vitaCouchEffectY;
#endif

    // True if the GPU doesn't support NPOT textures (GL < 2.0), requiring
    // FBO color-attachment textures to have power-of-two dimensions.
    bool needsPOT;

    // Blending mode + factors
    int32_t currentBlendMode;
    int32_t currentSFactor;
    int32_t currentDFactor;
    int32_t currentSFactorAlpha;
    int32_t currentDFactorAlpha;
} GLLegacyRenderer;

bool GLLegacyRenderer_ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId);
#ifdef PLATFORM_VITA
typedef void (*VitaTexturePrepareProgress)(uint32_t current, uint32_t total, void* user);
typedef void (*VitaShaderPrepareProgress)(uint32_t current, uint32_t total, void* user);
uint32_t GLLegacyRenderer_prepareShaders(GLLegacyRenderer* gl, VitaShaderPrepareProgress progress, void* user);
bool GLLegacyRenderer_experimentalShadersEnabled(const GLLegacyRenderer* gl);
bool GLLegacyRenderer_textureCacheIsComplete(const DataWin* dataWin);
uint32_t GLLegacyRenderer_prepareTextureCache(DataWin* dataWin,
                                              VitaTexturePrepareProgress progress,
                                              void* user);
void GLLegacyRenderer_trimTextureCacheForRoomChange(GLLegacyRenderer* gl,
                                                     uint64_t targetBytes,
                                                     bool includeSmallPages);
void GLLegacyRenderer_collectRetiredTexturesForRoomChange(GLLegacyRenderer* gl);
void GLLegacyRenderer_prepareRoomTextureSet(GLLegacyRenderer* gl, Room* room);
uint32_t GLLegacyRenderer_preloadRoomTextureSet(GLLegacyRenderer* gl);
uint32_t GLLegacyRenderer_preloadChapterBattleCore(GLLegacyRenderer* gl);
#endif
Renderer* GLLegacyRenderer_create(void);

#endif /* _BS_GL_LEGACY_RENDERER_H_ */
