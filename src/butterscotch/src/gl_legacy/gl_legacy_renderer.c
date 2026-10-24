#include "gl_legacy_renderer.h"
#include "image_decoder.h"
#include "camera_quad.h"
#include <ctype.h>

#ifdef PLATFORM_VITA
#include "vita_gpu_rules.h"
extern int g_vitaTextureLinearFilter;
extern int g_vitaTextureFilterRevision;
static GLenum vitaTextureFilter(void) {
    return g_vitaTextureLinearFilter ? GL_LINEAR : GL_NEAREST;
}
#else
// The same upload paths are compiled by local-test. Keep their filtering
// deterministic without requiring the Vita settings overlay at link time.
static GLenum vitaTextureFilter(void) {
    return GL_NEAREST;
}
#endif

#ifdef PLATFORM_VITA
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
// The bundled no-splash VitaGL exports this helper, while some installed
// VitaSDK headers omit its declaration.
extern void vglForceGarbageCollection(void);
// vitaGL already builds stb_dxt for its runtime compressed-texture path. Reuse
// the same encoder instead of carrying a second implementation in the runner.
extern void stb_compress_dxt_block(unsigned char* dest,
                                   const unsigned char* src_rgba_four_bytes_per_pixel,
                                   int alpha, int mode);
#define VITA_TX_CACHE_ROOT "ux0:data/deltarune/deltarunevita/texture-cache"
#define VITA_PVR_ROOT "ux0:data/deltarune/deltarunevita/pvr"
#define VITA_PVR_RGBA4444_FORMAT 0x0404040461626772ULL
#define VITA_PVR_RGBA5551_FORMAT 0x0105050561626772ULL
#define VITA_PVR_PVRTC1_RGBA_4BPP_FORMAT 3ULL
#define VITA_PVR_PVRTC2_RGBA_4BPP_FORMAT 5ULL
#define VITA_PVR_BC3_DXT5_FORMAT 11ULL
// Runtime graphics setting. RGBA4444 remains the safe default; PVRTC2 can be
// enabled explicitly from Game Settings and takes effect after a restart.
extern int g_vitaPvrEnabled;
extern int g_vitaBc3OnlyEnabled;
// Phase 5: set to 1 by Game Settings when the Texture Compression mode changes,
// so the next frame releases resident format-dependent pages and reloads them in
// the new format. Defined in vita_settings.c alongside the other graphics flags.
extern int g_vitaTextureCacheInvalidate;
static bool vitaEvictSpecificTexturePage(GLLegacyRenderer* gl, uint32_t victim);
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG 0x8C02
#endif
#ifndef GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG
#define GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG 0x9138
#endif

typedef struct {
    uint32_t magic;
    uint32_t sourceSize;
    uint32_t sourceOffset;
    uint32_t width;
    uint32_t height;
} VitaTextureCacheHeader;

#define VITA_TX_CACHE_MAGIC 0x31435456U /* VTC1 */
#define VITA_TX_CACHE_COMPLETE_MAGIC 0x32435456U /* VTC2 */
#define VITA_TX_CACHE_CH3_MAGIC 0x35435456U /* VTC5 */
#define VITA_TX_CACHE_CH3_COMPLETE_MAGIC 0x36435456U /* VTC6 */
#define VITA_BC3_CACHE_COMPLETE_MAGIC 0x33434256U /* VBC3 */

typedef struct {
    uint32_t magic;
    uint32_t textureCount;
    uint32_t sourceFingerprint;
    uint32_t pvrFileCount;
} VitaBc3CacheComplete;

static bool vitaBc3ForceRegenerate = false;

// One lossless 2048px cache page needs an 8 MiB upload buffer. The regular C
// heap is intentionally small after parsing Chapters 3-5, while the Vita still
// has tens of MiB of user main memory available. Keep a single transient block
// outside that heap. Keep one reusable block: repeatedly allocating/freeing an
// 8 MiB page fragments USER_RW and made every cache page after the first fail.
static SceUID vitaTextureStagingBlock = -1;
static void* vitaTextureStagingBase = nullptr;
static size_t vitaTextureStagingBytes = 0;

static void* vitaAcquireTextureStaging(size_t bytes) {
    if (vitaTextureStagingBase != nullptr && vitaTextureStagingBytes >= bytes)
        return vitaTextureStagingBase;
    if (vitaTextureStagingBlock >= 0) return nullptr;
    // Allocate the full lossless-page capacity even when the first request is
    // a 2 MiB PVR, so a later 8 MiB RGBA4444 upload can reuse the same block.
    size_t blockBytes = bytes < 8U * 1024U * 1024U ?
                        8U * 1024U * 1024U : bytes;
    blockBytes = (blockBytes + 4095U) & ~(size_t)4095U;
    vitaTextureStagingBlock = sceKernelAllocMemBlock(
        "deltarune_texture_staging", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
        blockBytes, nullptr);
    if (vitaTextureStagingBlock < 0 ||
        sceKernelGetMemBlockBase(vitaTextureStagingBlock,
                                 &vitaTextureStagingBase) < 0) {
        if (vitaTextureStagingBlock >= 0)
            sceKernelFreeMemBlock(vitaTextureStagingBlock);
        vitaTextureStagingBlock = -1;
        vitaTextureStagingBase = nullptr;
        vitaTextureStagingBytes = 0;
        return nullptr;
    }
    vitaTextureStagingBytes = blockBytes;
    return vitaTextureStagingBase;
}

static void vitaReleaseTexturePayload(void* payload, bool uploaded) {
    if (payload == nullptr) return;
    if (uploaded) glFinish();
    if (payload != vitaTextureStagingBase) free(payload);
}

static void vitaTextureLog(uint32_t pageId, int w, int h, GLint maxTextureSize, const char* phase);
static int vitaTextureChapter(const DataWin* dw);
static const char* vitaTextureCacheVariant(const DataWin* dw);
static void vitaTextureCachePath(const DataWin* dw, uint32_t pageId,
                                 char* output, size_t outputSize);

static bool vitaPreparedTextureExists(const DataWin* dw, uint32_t pageId);

// 0=generic, 1=Light World, 2=Castle, 3=Cyber/City, 4=Mansion.
// Chapter 2 reuses atlas IDs across unrelated regions; selecting lossless
// pages by active region avoids carrying several 8 MiB RGBA4444 pages into
// Cyber rooms that only need their PVR-safe content.
static int vitaCh2TexturePolicyRegion = 0;
static bool vitaFloweryBattleMemoryMode = false;
static bool vitaChapter3TvMemoryMode = false;
static bool vitaChapter3BoardMinigameMode = false;
static bool vitaChapter3PerformanceSurfaceMode = false;
static bool vitaChapter3GreenRoomMode = false;
static bool vitaChapter5CastleApproachMode = false;
static bool vitaNativeVideoMemoryMode = false;
static bool vitaCouchOverworldMemoryMode = false;
static bool vitaCh2CyberIntroMemoryMode = false;
static bool vitaCh2KeyboardMemoryMode = false;
static bool vitaCh2BattleMemoryMode = false;
static bool vitaCh2GigaSequenceMode = false;
// Experimental room-local BC3 profile. Keeping this scoped to the teacup
// room lets us validate native UBC3 sampling and memory use without changing
// the already reviewed PVRTC2 pages in the rest of Chapter 2.
static bool vitaCh2TeacupBc3Mode = false;

static bool vitaCh2TeacupBc3Page(uint32_t pageId) {
    // Only replace pages that otherwise need the 8 MiB RGBA4444 path in this
    // room. Pages 12/15/17/19 already have a 2 MiB PVRTC2 representation;
    // changing those to 4 MiB BC3 increased pressure without saving memory.
    return pageId == 6U || pageId == 21U;
}

// Phase 6 semantic detection. A Texture Page must stay lossless (RGBA4444, no
// BC3) when it holds font glyphs or textbox/UI frames, because BC3's 4x4 block
// interpolation alters thin glyphs and the white textbox pixels. Resolve the
// pages purely from the active data.win (FONT chunk -> tpag -> page for fonts,
// sprite named "spr_textbox" -> tpag -> page for UI), NEVER by hardcoded Texture
// Page ID: IDs, counts and dimensions all changed in the repack. Works for the
// base game and every translated variant.
static bool vitaSemanticFontOrUiPage(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr) return false;
    for (uint32_t i = 0; i < dw->font.count; ++i) {
        const Font* font = &dw->font.fonts[i];
        if (!font->present || font->tpagIndex < 0 ||
            (uint32_t)font->tpagIndex >= dw->tpag.count) continue;
        if ((uint32_t)dw->tpag.items[font->tpagIndex].texturePageId == pageId)
            return true;
    }
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* sprite = &dw->sprt.sprites[i];
        if (!sprite->present || sprite->name == nullptr || sprite->tpagIndices == nullptr)
            continue;
        const bool criticalSprite =
            strncmp(sprite->name, "spr_textbox", 11) == 0 ||
            (vitaTextureChapter(dw) == 3 &&
             (strstr(sprite->name, "logo") != nullptr ||
              strstr(sprite->name, "title") != nullptr));
        if (!criticalSprite) continue;
        for (uint32_t frame = 0; frame < sprite->textureCount; ++frame) {
            int32_t tpagIndex = sprite->tpagIndices[frame];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            if ((uint32_t)dw->tpag.items[tpagIndex].texturePageId == pageId)
                return true;
        }
    }
    return false;
}

// Compatibility wrapper: the same semantic detection, gated to translated
// variants (preserves the previous variant-only callers unchanged).
static bool vitaTranslatedUiRequiresLossless(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr || vitaTextureCacheVariant(dw)[0] == '\0') return false;
    return vitaSemanticFontOrUiPage(dw, pageId);
}

// BC3 is appropriate for most 2048px scenery, but its 4x4 colour blocks can
// visibly alter one-pixel geometry.  Keep only atlas pages containing assets
// whose meaning depends on those exact pixels lossless.  Resolve by resource
// name so the policy also follows the independently repacked PT-BR data.win.
static bool vitaPixelCriticalPage(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr) return false;
    // Phase 6: semantic font/UI detection now applies to the BASE game too
    // (was variant-only), so BC3 never touches glyph/textbox pages regardless
    // of language.
    if (vitaSemanticFontOrUiPage(dw, pageId)) return true;

    if (vitaTextureChapter(dw) == 2) {
        static const char* exactBackgrounds[] = {
            // Thin Cyber World floor/grid lines showed BC3 block-colour
            // contamination in room_dw_cyber_viro_ring.
            "bg_dw_cyber_lines_tileset"
        };
        for (uint32_t wanted = 0;
             wanted < sizeof(exactBackgrounds) / sizeof(exactBackgrounds[0]);
             ++wanted) {
            for (uint32_t i = 0; i < dw->bgnd.count; ++i) {
                const Background* bg = &dw->bgnd.backgrounds[i];
                if (!bg->present || bg->name == nullptr ||
                    strcmp(bg->name, exactBackgrounds[wanted]) != 0 ||
                    bg->tpagIndex < 0 ||
                    (uint32_t)bg->tpagIndex >= dw->tpag.count) continue;
                if ((uint32_t)dw->tpag.items[bg->tpagIndex].texturePageId == pageId)
                    return true;
            }
        }
    }
    return false;
}


// Chapter 3 TV/game-show relief set for the Optimized profile. These are
// decorative/special-presentation sprites which otherwise occupy lossless
// RGBA4444 pages during the heaviest TV sequence. Prefer BC3 for any page that
// contains one of them, but only after vitaPixelCriticalPage() has had the
// chance to protect fonts and spr_textbox* pages.
static bool vitaChapter3OptimizedBc3Page(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr || vitaTextureChapter(dw) != 3) return false;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* sprite = &dw->sprt.sprites[i];
        if (!sprite->present || sprite->name == nullptr || sprite->tpagIndices == nullptr)
            continue;
        const char* name = sprite->name;
        bool candidate =
            strcmp(name, "spr_gameshow_ralseiplush_stoole") == 0 ||
            strcmp(name, "spr_quiz_ralsei_plush") == 0 ||
            strncmp(name, "spr_funnytext_", 14) == 0 ||
            strncmp(name, "spr_ja_funnytext_", 17) == 0 ||
            strcmp(name, "spr_dw_tv_time_funnytext") == 0;
        if (!candidate) continue;
        for (uint32_t frame = 0; frame < sprite->textureCount; ++frame) {
            int32_t tpagIndex = sprite->tpagIndices[frame];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            if ((uint32_t)dw->tpag.items[tpagIndex].texturePageId == pageId)
                return true;
        }
    }
    return false;
}
#define VITA_DATA_ROOT "ux0:data/deltarune/deltarunevita"

// Maps the active data.win variant to its language mod folder, or nullptr for
// the base game. Used to build the per-chapter, self-contained cache roots.
static const char* vitaTextureLangFolder(const DataWin* dw) {
    const char* v = vitaTextureCacheVariant(dw);
    if (strcmp(v, "-ptbr") == 0) return "Portuguese-BR";
    if (strcmp(v, "-spa")  == 0) return "Spanish";
    if (strcmp(v, "-ita")  == 0) return "Italian";
    if (strcmp(v, "-tur")  == 0) return "Turkish";
    if (strcmp(v, "-ger")  == 0) return "German";
    if (strcmp(v, "-rus")  == 0) return "Russkiy";
    if (strcmp(v, "-fra")  == 0) return "French";
    return nullptr;
}

// Phase 1 (chapter-first, self-contained layout). Per-chapter cache root:
//   base game:     ux0:data/deltarune/deltarunevita/chapterN
//   language mod:  ux0:data/deltarune/deltarunevita/mods/Lang/<Language>/chapterN
// R444 pages live under <root>/texture-cache/, BC3/PVR pages under <root>/pvr/.
static void vitaChapterRoot(const DataWin* dw, char* output, size_t outputSize) {
    const char* lang = vitaTextureLangFolder(dw);
    if (lang != nullptr)
        snprintf(output, outputSize, VITA_DATA_ROOT "/mods/Lang/%s/chapter%d",
                 lang, vitaTextureChapter(dw));
    else
        snprintf(output, outputSize, VITA_DATA_ROOT "/chapter%d", vitaTextureChapter(dw));
}

// These Chapter 2 pages are selected as RGBA4444 in at least one region.
// Keep a lossless companion even when a PVR exists; otherwise the startup
// preparer marks the cache complete, while the runtime rejects that PVR and
// leaves the entire atlas absent (page 17 contains bg_alphysclass, Seam,
// spr_castle_cafe and several shared backgrounds).
static bool vitaPvrRequiresLosslessCompanion(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr) return false;
    // BC3-only already has a GPU-ready page for every atlas. Do not let old
    // PVRTC safety markers make the startup pass decode duplicate RGBA4444
    // pages and show the cache progress bar on every clean installation.
    // Only translated font/textbox pages intentionally remain lossless.
    if (g_vitaBc3OnlyEnabled)
        return vitaPixelCriticalPage(dw, pageId);
    if (vitaPixelCriticalPage(dw, pageId)) return true;
    char markerPath[256];
    char markerRoot[192];
    vitaChapterRoot(dw, markerRoot, sizeof(markerRoot));
    snprintf(markerPath, sizeof(markerPath), "%s/pvr/page_%03u.rgba4444", markerRoot, pageId);
    SceIoStat markerStat;
    if (sceIoGetstat(markerPath, &markerStat) >= 0) return true;
    if (vitaTextureChapter(dw) != 2) return false;
    // TODO(remove-after-Ch2-validation): legacy hardcoded Chapter 2 Texture Page
    // IDs, kept ONLY as a fallback while the semantic detector
    // (vitaSemanticFontOrUiPage, applied above via vitaPixelCriticalPage) is
    // validated on Chapter 2 hardware. The repack changed page IDs, so these are
    // no longer reliable; delete once telemetry confirms the semantic set covers
    // them. Reaching here means the semantic detector did NOT flag this page.
    bool legacyId = pageId == 12U || pageId == 14U || pageId == 16U || pageId == 17U ||
                    pageId == 24U || pageId == 25U || pageId == 26U || pageId == 29U;
    if (legacyId)
        vitaTextureLog(pageId, 0, 0, 0, "classify_legacy_only_semantic_missed_ch2");
    return legacyId;
}

extern int g_vitaTextureFormatProfile;

static bool vitaPvrTextureAllowed(const DataWin* dw, uint32_t pageId) {
    if (!g_vitaPvrEnabled || dw == nullptr || pageId >= dw->txtr.count)
        return false;

    // Translated fonts and dialogue frames always stay in RGBA4444
    if (vitaPixelCriticalPage(dw, pageId)) return false;

    // Profile 1: Nativo (Native) -> 100% RGBA4444
    if (g_vitaTextureFormatProfile == 1) return false;

    // Flowery is the Chapter 5 exception to the normal hybrid policy. The
    // measured final-battle working set reached ~71 MiB of RGBA4444 atlases
    // plus ~22 MiB across 38 composition surfaces, leaving under 3 MiB in the
    // VitaGL pool and causing 102 evictions/reloads. All PC-prepared pages have
    // a matching BC3 representation, while vitaPixelCriticalPage() above keeps
    // fonts, UI and dialogue frames lossless. Compress the remaining battle
    // pages even in Optimized mode to remove that eviction loop without making
    // the whole Chapter use the Aggressive profile.
    if (g_vitaTextureFormatProfile == 0 && vitaFloweryBattleMemoryMode)
        return true;

    // Profile 0: Otimizado -> BC3 ONLY for exactly-2048x2048 Texture Pages
    // (the unbreakable atlases the offline optimizer could not split), RGBA4444
    // for everything smaller. Decision is by real page dimension, never by
    // Texture Page ID (IDs changed in the repack), and never by blob size.
    if (g_vitaTextureFormatProfile == 0) {
        // RectPOT turns most of Chapter 3's former 2048px TV/game-show pages
        // into 1024x1024 or 512x512 atlases. Keeping the old "BC3 only at
        // 2048" rule therefore changed those pages back to R444, doubling the
        // measured board working set to ~74 MiB and producing 120-570 ms
        // uploads during the minigame. Fonts/textboxes have already been
        // rejected above by vitaPixelCriticalPage(), so compress the remaining
        // presentation pages while this sequence is active.
        if (vitaChapter3TvMemoryMode) {
            const Texture* tvTxtr = &dw->txtr.textures[pageId];
            if (tvTxtr != nullptr &&
                tvTxtr->textureWidth >= 512 && tvTxtr->textureHeight >= 512)
                return true;
        }
        // Chapter 3 TV/game-show one-scene props and Tenna special cards can
        // use BC3 to preserve RAM for the lossless dialogue frame.
        if (vitaChapter3OptimizedBc3Page(dw, pageId)) return true;
        const Texture* txtr = &dw->txtr.textures[pageId];
        return txtr != nullptr &&
               txtr->textureWidth == 2048 && txtr->textureHeight == 2048;
    }

    // Profile 2: Low -> BC3 for all available pages
    if (g_vitaTextureFormatProfile == 2) {
        return true;
    }

    return true;
}

static void vitaPvrPath(const DataWin* dw, uint32_t pageId, char* output, size_t outputSize) {
    // The v0.71 preparation pipeline emits BC3 as page_NNN.bc3.pvr for every
    // profile. The profile controls which pages may use it, not the filename.
    // Looking for page_NNN.pvr in Optimized mode silently missed every prepared
    // page and forced expensive R444/data.win uploads instead.
    const char* suffix = ".bc3.pvr";
    // Phase 1: chapter-first self-contained layout. Language mods mirror the
    // base under mods/Lang/<Language>/chapterN/pvr/ (handled by vitaChapterRoot).
    char root[192];
    vitaChapterRoot(dw, root, sizeof(root));
    snprintf(output, outputSize, "%s/pvr/page_%03u%s", root, pageId, suffix);
}

static void vitaPvrBasePath(const DataWin* dw, uint32_t pageId, char* output, size_t outputSize) {
    const char* suffix = ".bc3.pvr";
    // Always the BASE game chapter (never a language mod): used as the fallback
    // source when a language-variant page is absent.
    snprintf(output, outputSize, VITA_DATA_ROOT "/chapter%d/pvr/page_%03u%s",
             vitaTextureChapter(dw), pageId, suffix);
}

static bool vitaVariantPageDiffers(const DataWin* dw, uint32_t pageId) {
    const char* variant = vitaTextureCacheVariant(dw);
    if (variant[0] == '\0') return false;
    (void)pageId;
    // Every language package produced by the current rebuild pipeline owns an
    // independently packed atlas set. Page IDs therefore describe that
    // language's data.win, not the official build. Borrowing a base page by the
    // same numeric ID can display unrelated artwork; a missing translated page
    // must continue through its own R444/data.win fallback instead.
    return true;
}

static bool vitaReadPvr(const DataWin* dw, uint32_t pageId,
                           uint8_t** payload, uint32_t* payloadSize,
                           int* width, int* height, uint64_t* pixelFormat) {
    if (!vitaPvrTextureAllowed(dw, pageId)) return false;
    char path[256];
    // PVRs are generated by the desktop patcher from the supported Steam data
    // and live exclusively in ux0:data. PVR accepts only native PVRTC2 sRGB;
    // RGBA4444 belongs to texture-cache and data.win is never a pixel fallback.
    vitaPvrPath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    // PT-BR changes only its explicitly exported atlas pages. The castle
    // scenery pages 10-12 are byte-identical to the official Chapter 2 data,
    // so reuse the base compressed PVR instead of decoding the modded
    // data.win merely because the variant directory has no duplicate copy.
    if (fd < 0 && vitaTextureCacheVariant(dw)[0] != '\0' &&
        !vitaVariantPageDiffers(dw, pageId)) {
        vitaPvrBasePath(dw, pageId, path, sizeof(path));
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    }
    if (fd < 0) return false;
    uint8_t header[52];
    if (sceIoRead(fd, header, sizeof(header)) != sizeof(header)) {
        sceIoClose(fd);
        return false;
    }
    uint32_t magic, flags, colorSpace, channelType, w, h, depth, surfaces, faces, mipmaps, metadata;
    uint64_t format;
    memcpy(&magic, header + 0, 4);
    memcpy(&flags, header + 4, 4);
    memcpy(&format, header + 8, 8);
    memcpy(&colorSpace, header + 16, 4);
    memcpy(&channelType, header + 20, 4);
    memcpy(&h, header + 24, 4);
    memcpy(&w, header + 28, 4);
    memcpy(&depth, header + 32, 4);
    memcpy(&surfaces, header + 36, 4);
    memcpy(&faces, header + 40, 4);
    memcpy(&mipmaps, header + 44, 4);
    memcpy(&metadata, header + 48, 4);
    bool pvrtc4bpp = format == VITA_PVR_PVRTC2_RGBA_4BPP_FORMAT;
    bool bc3 = format == VITA_PVR_BC3_DXT5_FORMAT;
    uint64_t expected = pvrtc4bpp ?
        ((uint64_t)w * (uint64_t)h / 2ULL) :
        (((uint64_t)w + 3ULL) / 4ULL) * (((uint64_t)h + 3ULL) / 4ULL) * 16ULL;
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    SceOff dataOffset = 52 + (SceOff)metadata;
    const Texture* sourceTexture = pageId < dw->txtr.count ? &dw->txtr.textures[pageId] : nullptr;
    bool dimensionsMatchSource = sourceTexture != nullptr &&
        (sourceTexture->textureWidth <= 0 || (uint32_t)sourceTexture->textureWidth == w) &&
        (sourceTexture->textureHeight <= 0 || (uint32_t)sourceTexture->textureHeight == h);
    if (magic != 0x03525650U || flags != 0U || (!pvrtc4bpp && !bc3) ||
        colorSpace != 1U || channelType != 0U || depth != 1U ||
        w == 0 || h == 0 || w > 4096 || h > 4096 ||
        !dimensionsMatchSource ||
        surfaces != 1 || faces != 1 || mipmaps != 1 ||
        expected > UINT32_MAX || fileSize - dataOffset != (SceOff)expected ||
        sceIoLseek(fd, dataOffset, SCE_SEEK_SET) < 0) {
        sceIoClose(fd);
        return false;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)expected);
    if (data == nullptr)
        data = (uint8_t*)vitaAcquireTextureStaging((size_t)expected);
    if (data == nullptr || sceIoRead(fd, data, (unsigned int)expected) != (int)expected) {
        vitaReleaseTexturePayload(data, false);
        sceIoClose(fd);
        return false;
    }
    sceIoClose(fd);
    *payload = data;
    *payloadSize = (uint32_t)expected;
    *width = (int)w;
    *height = (int)h;
    *pixelFormat = format;
    return true;
}

static bool vitaPvrTextureIsAvailable(const DataWin* dw, uint32_t pageId) {
    uint8_t* payload = nullptr;
    uint32_t payloadSize = 0;
    int width = 0, height = 0;
    uint64_t pixelFormat = 0;
    bool ok = vitaReadPvr(dw, pageId, &payload, &payloadSize, &width, &height, &pixelFormat);
    vitaReleaseTexturePayload(payload, false);
    return ok;
}

static uint64_t vitaPvrTextureEncodedBytes(const DataWin* dw, uint32_t pageId) {
    if (!vitaPvrTextureAllowed(dw, pageId)) return 0;

    // Preflight needs only the format and encoded size.  Calling vitaReadPvr
    // here used to malloc/read the complete 2 MiB payload, then discard it
    // before the real upload.  In Chapter 5 the fragmented staging heap could
    // not satisfy that temporary allocation, so a valid PVRTC2 page was
    // misclassified as an 8 MiB RGBA4444 page and deferred every frame.
    char path[256];
    vitaPvrPath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0 && vitaTextureCacheVariant(dw)[0] != '\0' &&
        !vitaVariantPageDiffers(dw, pageId)) {
        vitaPvrBasePath(dw, pageId, path, sizeof(path));
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    }
    if (fd < 0) return 0;

    uint8_t header[52];
    if (sceIoRead(fd, header, sizeof(header)) != sizeof(header)) {
        sceIoClose(fd);
        return 0;
    }
    uint32_t magic, flags, colorSpace, channelType, w, h, depth, surfaces, faces, mipmaps, metadata;
    uint64_t format;
    memcpy(&magic, header + 0, 4);
    memcpy(&flags, header + 4, 4);
    memcpy(&format, header + 8, 8);
    memcpy(&colorSpace, header + 16, 4);
    memcpy(&channelType, header + 20, 4);
    memcpy(&h, header + 24, 4);
    memcpy(&w, header + 28, 4);
    memcpy(&depth, header + 32, 4);
    memcpy(&surfaces, header + 36, 4);
    memcpy(&faces, header + 40, 4);
    memcpy(&mipmaps, header + 44, 4);
    memcpy(&metadata, header + 48, 4);
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);

    const Texture* source = pageId < dw->txtr.count ? &dw->txtr.textures[pageId] : nullptr;
    bool dimensionsMatch = source != nullptr &&
        (source->textureWidth <= 0 || (uint32_t)source->textureWidth == w) &&
        (source->textureHeight <= 0 || (uint32_t)source->textureHeight == h);
    bool pvrtc4bpp = format == VITA_PVR_PVRTC2_RGBA_4BPP_FORMAT;
    bool bc3 = format == VITA_PVR_BC3_DXT5_FORMAT;
    uint64_t expected = pvrtc4bpp ?
        ((uint64_t)w * (uint64_t)h / 2ULL) :
        (((uint64_t)w + 3ULL) / 4ULL) * (((uint64_t)h + 3ULL) / 4ULL) * 16ULL;
    bool valid = magic == 0x03525650U && flags == 0U &&
           (pvrtc4bpp || bc3) && colorSpace == 1U && channelType == 0U && depth == 1U &&
           w > 0 && h > 0 && w <= 4096 && h <= 4096 && dimensionsMatch &&
           surfaces == 1U && faces == 1U && mipmaps == 1U &&
           fileSize == (SceOff)(52ULL + metadata + expected);
    return valid ? expected : 0;
}

static uint32_t vitaBc3SourceFingerprint(const DataWin* dw) {
    // FNV-1a over the TXTR layout. A rebuilt data.win with different offsets,
    // dimensions or payload sizes invalidates a runtime-generated BC3 marker
    // even when it happens to retain the same number of Texture Pages.
    uint32_t hash = 2166136261U;
    if (dw == nullptr) return hash;
    for (uint32_t pageId = 0; pageId < dw->txtr.count; ++pageId) {
        const Texture* txtr = &dw->txtr.textures[pageId];
        uint32_t values[5] = {
            pageId, txtr->blobSize, txtr->blobOffset,
            (uint32_t)txtr->textureWidth, (uint32_t)txtr->textureHeight
        };
        for (size_t i = 0; i < sizeof(values); ++i) {
            hash ^= ((const uint8_t*)values)[i];
            hash *= 16777619U;
        }
    }
    return hash;
}

static void vitaPvrDir(const DataWin* dw, char* output, size_t outputSize) {
    char root[192];
    vitaChapterRoot(dw, root, sizeof(root));
    snprintf(output, outputSize, "%s/pvr", root);
}

static void vitaBc3CompletePath(const DataWin* dw, char* output, size_t outputSize) {
    char dir[224];
    vitaPvrDir(dw, dir, sizeof(dir));
    snprintf(output, outputSize, "%s/complete.bc3.vtc", dir);
}

static uint32_t vitaCountBc3Files(const DataWin* dw) {
    char dir[224];
    vitaPvrDir(dw, dir, sizeof(dir));
    SceUID dd = sceIoDopen(dir);
    if (dd < 0) return 0;
    uint32_t count = 0;
    SceIoDirent entry;
    memset(&entry, 0, sizeof(entry));
    while (sceIoDread(dd, &entry) > 0) {
        size_t length = strlen(entry.d_name);
        static const char suffix[] = ".bc3.pvr";
        if (length > sizeof(suffix) - 1U &&
            strcmp(entry.d_name + length - (sizeof(suffix) - 1U), suffix) == 0)
            count++;
        memset(&entry, 0, sizeof(entry));
    }
    sceIoDclose(dd);
    return count;
}

static void vitaMarkBc3CacheComplete(const DataWin* dw) {
    char dir[224];
    vitaPvrDir(dw, dir, sizeof(dir));
    sceIoMkdir(dir, 0777);
    char path[256];
    vitaBc3CompletePath(dw, path, sizeof(path));
    VitaBc3CacheComplete marker = {
        VITA_BC3_CACHE_COMPLETE_MAGIC,
        dw != nullptr ? dw->txtr.count : 0U,
        vitaBc3SourceFingerprint(dw),
        vitaCountBc3Files(dw)
    };
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    sceIoWrite(fd, &marker, sizeof(marker));
    sceIoClose(fd);
}

static bool vitaReadBc3Marker(const DataWin* dw, VitaBc3CacheComplete* marker) {
    char path[256];
    vitaBc3CompletePath(dw, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    memset(marker, 0, sizeof(*marker));
    int got = sceIoRead(fd, marker, sizeof(*marker));
    sceIoClose(fd);
    return got == sizeof(*marker) && marker->magic == VITA_BC3_CACHE_COMPLETE_MAGIC;
}

static bool vitaBc3SetIsComplete(const DataWin* dw) {
    if (dw == nullptr) return false;
    VitaBc3CacheComplete marker;
    if (vitaReadBc3Marker(dw, &marker)) {
        uint32_t fingerprint = vitaBc3SourceFingerprint(dw);
        if (marker.textureCount != dw->txtr.count ||
            marker.sourceFingerprint != fingerprint) {
            // A rebuilt data.win can retain the same page dimensions while its
            // atlas contents change completely. Re-encode every eligible page.
            vitaBc3ForceRegenerate = true;
            return false;
        }
        if (marker.pvrFileCount == vitaCountBc3Files(dw))
            return true;
        // A page was removed after completion. The preparation loop will fill
        // only the missing file and refresh the marker.
        return false;
    }

    // Adopt complete PC-generated sets without regenerating them. This is a
    // header-only scan and is performed once; the fingerprinted marker makes
    // later boots O(1). Pixel-critical font/UI pages deliberately remain R444.
    for (uint32_t pageId = 0; pageId < dw->txtr.count; ++pageId) {
        if (vitaPvrTextureAllowed(dw, pageId)) {
            if (vitaPvrTextureEncodedBytes(dw, pageId) == 0) return false;
        } else if (!vitaPreparedTextureExists(dw, pageId)) {
            return false;
        }
    }
    vitaMarkBc3CacheComplete(dw);
    vitaBc3ForceRegenerate = false;
    return true;
}

static bool vitaWriteBc3Block(SceUID fd, uint8_t* buffered, size_t* used,
                              const uint8_t block[16]) {
    const size_t capacity = 64U * 1024U;
    if (*used + 16U > capacity) {
        if (sceIoWrite(fd, buffered, (unsigned int)*used) != (int)*used)
            return false;
        *used = 0;
    }
    memcpy(buffered + *used, block, 16U);
    *used += 16U;
    return true;
}

static bool vitaGenerateBc3Pvr(const DataWin* dw, uint32_t pageId,
                               int width, int height, const uint8_t* rgba,
                               VitaTexturePrepareProgress progress, void* user) {
    if (dw == nullptr || rgba == nullptr || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096 || !vitaPvrTextureAllowed(dw, pageId))
        return false;

    char dir[224];
    vitaPvrDir(dw, dir, sizeof(dir));
    sceIoMkdir(dir, 0777);
    char finalPath[256];
    vitaPvrPath(dw, pageId, finalPath, sizeof(finalPath));
    char tempPath[272];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", finalPath);
    sceIoRemove(tempPath);

    SceUID fd = sceIoOpen(tempPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;

    uint8_t header[52] = {0};
    uint32_t version = 0x03525650U;
    uint64_t format = VITA_PVR_BC3_DXT5_FORMAT;
    uint32_t colorSpace = 1U, channelType = 0U;
    uint32_t h = (uint32_t)height, w = (uint32_t)width;
    uint32_t one = 1U, metadata = 0U;
    memcpy(header + 0, &version, 4);
    memcpy(header + 8, &format, 8);
    memcpy(header + 16, &colorSpace, 4);
    memcpy(header + 20, &channelType, 4);
    memcpy(header + 24, &h, 4);
    memcpy(header + 28, &w, 4);
    memcpy(header + 32, &one, 4); // depth
    memcpy(header + 36, &one, 4); // surfaces
    memcpy(header + 40, &one, 4); // faces
    memcpy(header + 44, &one, 4); // mipmaps
    memcpy(header + 48, &metadata, 4);
    bool ok = sceIoWrite(fd, header, sizeof(header)) == sizeof(header);

    uint8_t* buffered = ok ? (uint8_t*)malloc(64U * 1024U) : nullptr;
    if (buffered == nullptr) ok = false;
    size_t used = 0;
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    uint8_t sourceBlock[64];
    uint8_t encodedBlock[16];
    for (int by = 0; ok && by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            for (int py = 0; py < 4; ++py) {
                int sy = by * 4 + py;
                if (sy >= height) sy = height - 1;
                for (int px = 0; px < 4; ++px) {
                    int sx = bx * 4 + px;
                    if (sx >= width) sx = width - 1;
                    memcpy(sourceBlock + (py * 4 + px) * 4,
                           rgba + ((size_t)sy * (size_t)width + (size_t)sx) * 4U, 4U);
                }
            }
            // Mode 0 is stb_dxt's fast deterministic path. It preserves the
            // input sRGB byte values; the PVR header declares that same space.
            stb_compress_dxt_block(encodedBlock, sourceBlock, 1, 0);
            ok = vitaWriteBc3Block(fd, buffered, &used, encodedBlock);
        }
        // Keep the loading overlay/minigame responsive during multi-second
        // BC3 pages without turning every block row into a framebuffer swap.
        if (progress != nullptr && (by & 15) == 15)
            progress(pageId, dw->txtr.count, user);
    }
    if (ok && used > 0)
        ok = sceIoWrite(fd, buffered, (unsigned int)used) == (int)used;
    free(buffered);
    sceIoClose(fd);

    uint64_t expectedPayload = (uint64_t)blocksX * (uint64_t)blocksY * 16ULL;
    SceIoStat stat;
    if (!ok || sceIoGetstat(tempPath, &stat) < 0 ||
        (uint64_t)stat.st_size != 52ULL + expectedPayload) {
        sceIoRemove(tempPath);
        return false;
    }
    // Rename only a complete file. A power loss leaves at most a disposable
    // .tmp and never a truncated page_NNN.bc3.pvr.
    sceIoRemove(finalPath);
    if (sceIoRename(tempPath, finalPath) < 0) {
        sceIoRemove(tempPath);
        return false;
    }
    if (vitaPvrTextureEncodedBytes(dw, pageId) != expectedPayload) {
        sceIoRemove(finalPath);
        return false;
    }
    vitaTextureLog(pageId, width, height, 4096, "runtime_bc3_written");
    return true;
}

static bool vitaPvrTextureIsCompressed(const DataWin* dw, uint32_t pageId) {
    return vitaPvrTextureEncodedBytes(dw, pageId) != 0;
}

static int vitaTextureChapter(const DataWin* dw) {
    const char* path = dw != nullptr ? dw->lazyLoadFilePath : nullptr;
    if (path == nullptr) return 0;
    const char* marker = strstr(path, "chapter");
    if (marker == nullptr || marker[7] < '0' || marker[7] > '5') return 0;
    return marker[7] - '0';
}

static const char* vitaTextureCacheVariant(const DataWin* dw) {
    const char* path = dw != nullptr ? dw->lazyLoadFilePath : nullptr;
    if (path != nullptr && strstr(path, "mods/Lang/Portuguese-BR/") != nullptr) return "-ptbr";
    if (path != nullptr && strstr(path, "mods/Lang/Spanish/") != nullptr) return "-spa";
    if (path != nullptr && strstr(path, "mods/Lang/Italian/") != nullptr) return "-ita";
    if (path != nullptr && strstr(path, "mods/Lang/Turkish/") != nullptr) return "-tur";
    if (path != nullptr && strstr(path, "mods/Lang/German/") != nullptr) return "-ger";
    if (path != nullptr && (strstr(path, "mods/Lang/Russian/") != nullptr || strstr(path, "mods/Lang/Russkiy/") != nullptr)) return "-rus";
    if (path != nullptr && strstr(path, "mods/Lang/French/") != nullptr) return "-fra";
    return "";
}

static void vitaTextureCachePath(const DataWin* dw, uint32_t pageId, char* output, size_t outputSize) {
    char root[192];
    vitaChapterRoot(dw, root, sizeof(root));
    snprintf(output, outputSize, "%s/texture-cache/page_%03u.r444", root, pageId);
}

static void vitaTextureCacheDir(const DataWin* dw, char* output, size_t outputSize) {
    char root[192];
    vitaChapterRoot(dw, root, sizeof(root));
    snprintf(output, outputSize, "%s/texture-cache", root);
}

static uint32_t vitaTextureCacheMagic(const DataWin* dw) {
    // Chapter 3 VTC3 caches only identified their source by blob size and
    // offset. Those values can remain unchanged between data.win revisions,
    // allowing stale packed pixels to appear as corrupted TV art. VTC5 makes
    // every older Chapter 3 page rebuild once from the active game data.
    return vitaTextureChapter(dw) == 3 ? VITA_TX_CACHE_CH3_MAGIC : VITA_TX_CACHE_MAGIC;
}

static uint32_t vitaTextureCacheCompleteMagic(const DataWin* dw) {
    return vitaTextureChapter(dw) == 3 ?
           VITA_TX_CACHE_CH3_COMPLETE_MAGIC : VITA_TX_CACHE_COMPLETE_MAGIC;
}

static bool vitaPreparedTextureExists(const DataWin* dw, uint32_t pageId) {
    if (dw == nullptr || pageId >= dw->txtr.count) return false;
    char path[256];
    vitaTextureCachePath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    VitaTextureCacheHeader header;
    int got = sceIoRead(fd, &header, sizeof(header));
    SceOff fileSize = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoClose(fd);
    const Texture* txtr = &dw->txtr.textures[pageId];
    uint64_t expected = sizeof(header) +
                        (uint64_t)header.width * (uint64_t)header.height * sizeof(uint16_t);
    return got == sizeof(header) && header.magic == vitaTextureCacheMagic(dw) &&
           header.sourceSize == txtr->blobSize && header.sourceOffset == txtr->blobOffset &&
           header.width > 0 && header.height > 0 &&
           header.width <= 4096 && header.height <= 4096 &&
           (txtr->textureWidth <= 0 || header.width == (uint32_t)txtr->textureWidth) &&
           (txtr->textureHeight <= 0 || header.height == (uint32_t)txtr->textureHeight) &&
           expected <= INT32_MAX && fileSize == (SceOff)expected;
}

static bool vitaTextureCacheIsComplete(const DataWin* dw) {
    char path[256];
    char dir[192];
    vitaTextureCacheDir(dw, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/complete.vtc", dir);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;
    uint32_t marker[2] = {0, 0};
    int got = sceIoRead(fd, marker, sizeof(marker));
    sceIoClose(fd);
    if (got != sizeof(marker) || marker[0] != vitaTextureCacheCompleteMagic(dw) ||
        marker[1] != dw->txtr.count) return false;
    // Trust the current completion marker. Opening and validating every page
    // on the Vita memory card made large rebuilt chapters spend close to a
    // minute on the texture screen even when the PC builder had already
    // produced a complete set. A genuinely missing/corrupt page is still
    // rejected by the normal per-page loader and falls back to data.win, so
    // correctness no longer requires an O(page count) boot-time filesystem
    // scan.
    return true;
}

bool GLLegacyRenderer_textureCacheIsComplete(const DataWin* dw) {
    // Aggressive means every BC3-eligible page must exist before gameplay.
    // Font/UI pages remain lossless and are covered by the normal R444 set.
    if (g_vitaTextureFormatProfile == 2)
        return vitaBc3SetIsComplete(dw);
    return vitaTextureCacheIsComplete(dw);
}

static void vitaMarkTextureCacheComplete(const DataWin* dw) {
    char path[256];
    char dir[192];
    vitaTextureCacheDir(dw, dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/complete.vtc", dir);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    uint32_t marker[2] = {vitaTextureCacheCompleteMagic(dw), dw->txtr.count};
    sceIoWrite(fd, marker, sizeof(marker));
    sceIoClose(fd);
}

static uint16_t* vitaLoadPreparedTexture(const DataWin* dw, const Texture* txtr,
                                         uint32_t pageId, int* width, int* height) {
    char path[256];
    vitaTextureCachePath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        vitaTextureLog(pageId, 0, 0, 4096, "cache_file_absent");
        return nullptr;
    }
    VitaTextureCacheHeader header;
    int got = sceIoRead(fd, &header, sizeof(header));
    if (got != sizeof(header) || header.magic != vitaTextureCacheMagic(dw) ||
        header.sourceSize != txtr->blobSize || header.sourceOffset != txtr->blobOffset ||
        header.width == 0 || header.height == 0 || header.width > 4096 || header.height > 4096 ||
        (txtr->textureWidth > 0 && header.width != (uint32_t)txtr->textureWidth) ||
        (txtr->textureHeight > 0 && header.height != (uint32_t)txtr->textureHeight)) {
        sceIoClose(fd);
        vitaTextureLog(pageId, (int)header.width, (int)header.height, 4096,
                       "cache_header_invalid");
        return nullptr;
    }
    size_t bytes = (size_t)header.width * (size_t)header.height * sizeof(uint16_t);
    uint16_t* packed = (uint16_t*)malloc(bytes);
    if (packed == nullptr && vitaTextureStagingBase != nullptr &&
        vitaTextureStagingBytes >= bytes) {
        // The preceding upload fences this buffer before release, so it is
        // safe to overwrite it with the next prepared page.
        packed = (uint16_t*)vitaTextureStagingBase;
        vitaTextureLog(pageId, (int)header.width, (int)header.height, 4096,
                       "cache_staging_user_main_reused");
    }
    if (packed == nullptr && vitaTextureStagingBlock < 0) {
        packed = (uint16_t*)vitaAcquireTextureStaging(bytes);
        if (packed != nullptr) {
            vitaTextureLog(pageId, (int)header.width, (int)header.height, 4096,
                           "cache_staging_user_main");
        }
    }
    if (packed == nullptr) {
        sceIoClose(fd);
        vitaTextureLog(pageId, (int)header.width, (int)header.height, 4096,
                       "cache_staging_alloc_failed");
        return nullptr;
    }
    if (sceIoRead(fd, packed, (unsigned int)bytes) != (int)bytes) {
        if (packed == vitaTextureStagingBase && vitaTextureStagingBlock >= 0) {
            sceKernelFreeMemBlock(vitaTextureStagingBlock);
            vitaTextureStagingBlock = -1;
            vitaTextureStagingBase = nullptr;
            vitaTextureStagingBytes = 0;
        } else {
            free(packed);
        }
        sceIoClose(fd);
        vitaTextureLog(pageId, (int)header.width, (int)header.height, 4096,
                       "cache_read_failed");
        return nullptr;
    }
    sceIoClose(fd);
    *width = (int)header.width;
    *height = (int)header.height;
    return packed;
}

static void vitaSavePreparedTexture(const DataWin* dw, const Texture* txtr, uint32_t pageId,
                                    int width, int height, const uint16_t* packed) {
    // Phase 1: cache now lives under <chapterRoot>/texture-cache. The chapter
    // root already exists (its data.win is loaded from there); just create the
    // texture-cache subfolder for on-device generation (offline cache is primary).
    char chapterDir[192];
    vitaTextureCacheDir(dw, chapterDir, sizeof(chapterDir));
    sceIoMkdir(chapterDir, 0777);
    char path[256];
    vitaTextureCachePath(dw, pageId, path, sizeof(path));
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return;
    VitaTextureCacheHeader header = {
        vitaTextureCacheMagic(dw), txtr->blobSize, txtr->blobOffset,
        (uint32_t)width, (uint32_t)height
    };
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint16_t);
    sceIoWrite(fd, &header, sizeof(header));
    sceIoWrite(fd, packed, (unsigned int)bytes);
    sceIoClose(fd);
}

#ifdef PLATFORM_VITA
extern volatile int g_vitaTexturePrepareAbortForMinigame;
#endif

uint32_t GLLegacyRenderer_prepareTextureCache(DataWin* dw,
                                              VitaTexturePrepareProgress progress,
                                              void* user) {
    if (dw == nullptr || dw->txtr.count == 0) return 0;
    // Keep R444 and BC3 side by side on disk. The user can switch profiles
    // live, and deleting the lossless companion here made Native mode trust a
    // stale complete.vtc while its actual page file had already disappeared.
    if (GLLegacyRenderer_textureCacheIsComplete(dw)) return dw->txtr.count;
    uint32_t prepared = 0;
    bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
    for (uint32_t pageId = 0; pageId < dw->txtr.count; ++pageId) {
        Texture* txtr = &dw->txtr.textures[pageId];
        if (!vitaBc3ForceRegenerate && vitaPvrTextureEncodedBytes(dw, pageId) != 0 &&
            vitaPreparedTextureExists(dw, pageId)) {
            prepared++;
            vitaTextureLog(pageId, 0, 0, 4096, "preload_pvr_skip");
            if (progress != nullptr) progress(pageId + 1, dw->txtr.count, user);
#ifdef PLATFORM_VITA
            if (g_vitaTexturePrepareAbortForMinigame) return prepared;
#endif
            continue;
        }
        int w = 0, h = 0;
        // In Aggressive mode a valid R444 page is not a substitute for a
        // missing BC3 page: decode once, generate the requested compressed
        // representation and persist it before entering gameplay.
        bool needsBc3 = vitaPvrTextureAllowed(dw, pageId) &&
                        (vitaBc3ForceRegenerate ||
                         vitaPvrTextureEncodedBytes(dw, pageId) == 0);
        uint16_t* cached = needsBc3 ? nullptr :
            vitaLoadPreparedTexture(dw, txtr, pageId, &w, &h);
        if (cached != nullptr) {
            free(cached);
            prepared++;
            vitaTextureLog(pageId, w, h, 4096, "preload_cache_hit");
        } else {
            vitaTextureLog(pageId, 0, 0, 4096, "preload_decode_begin");
            DataWin_loadTxtrIfNeeded(dw, pageId);
            uint8_t* pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t)txtr->blobSize,
                                                        gm2022_5, &w, &h);
            if (pixels != nullptr && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
                bool generatedBc3 = false;
                if (vitaPvrTextureAllowed(dw, pageId)) {
                    generatedBc3 = vitaGenerateBc3Pvr(dw, pageId, w, h, pixels,
                                                      progress, user);
                }
                uint64_t pixelCount = (uint64_t)w * (uint64_t)h;
                // Persist the lossless companion as well. It makes later live
                // switching to Native deterministic and avoids decoding the
                // same commercial atlas a second time.
                uint16_t* packed = (uint16_t*)pixels;
                for (uint64_t i = 0; i < pixelCount; ++i) {
                    const uint8_t* src = &pixels[i * 4ULL];
                    uint16_t alpha4 = src[3] == 0 ? 0 : (uint16_t)((src[3] + 15U) >> 4);
                    if (alpha4 > 15U) alpha4 = 15U;
                    packed[i] = (uint16_t)(((uint16_t)(src[0] >> 4) << 12) |
                                           ((uint16_t)(src[1] >> 4) << 8) |
                                           ((uint16_t)(src[2] >> 4) << 4) | alpha4);
                    // A 4096x4096 lossless page can spend several seconds in
                    // this CPU conversion. Yield a redraw roughly every 256K
                    // pixels so the 750 ms loading animation does not freeze.
                    if (progress != nullptr && (i & 0x3FFFFULL) == 0x3FFFFULL)
                        progress(pageId, dw->txtr.count, user);
                }
                vitaSavePreparedTexture(dw, txtr, pageId, w, h, packed);
                if (generatedBc3 || vitaPreparedTextureExists(dw, pageId)) {
                    prepared++;
                    vitaTextureLog(pageId, w, h, 4096,
                                   generatedBc3 ? "preload_bc3_written" :
                                                  "preload_cache_written");
                }
            } else {
                vitaTextureLog(pageId, w, h, 4096, "preload_decode_skipped");
            }
            free(pixels);
            if (!txtr->mapped) {
                free(txtr->blobData);
                txtr->blobData = nullptr;
            }
        }
        if (progress != nullptr) progress(pageId + 1, dw->txtr.count, user);
#ifdef PLATFORM_VITA
        // Switch to the worker only after a full page is durable on disk. This
        // keeps the default path synchronous and avoids handing a partial .tmp
        // BC3/R444 page across threads when the player enables the minigame.
        if (g_vitaTexturePrepareAbortForMinigame) return prepared;
#endif
    }
    if (prepared == dw->txtr.count) {
        vitaMarkTextureCacheComplete(dw);
        if (g_vitaTextureFormatProfile == 2) {
            // Discard a stale fingerprint before the final physical scan. A
            // new marker is written only if every eligible PVR now validates.
            char markerPath[256];
            vitaBc3CompletePath(dw, markerPath, sizeof(markerPath));
            sceIoRemove(markerPath);
            vitaBc3ForceRegenerate = false;
            (void)vitaBc3SetIsComplete(dw);
        }
    }
    if (prepared == dw->txtr.count)
        vitaBc3ForceRegenerate = false;
    return prepared;
}

static void vitaTextureLog(uint32_t pageId, int w, int h, GLint maxTextureSize, const char* phase) {
    extern int g_vitaProbeLoggingEnabled;
    if (!g_vitaProbeLoggingEnabled) return;
    char line[160];
    int length = snprintf(line, sizeof(line), "TXTR page=%u size=%dx%d max=%d phase=%s\n",
                          pageId, w, h, (int) maxTextureSize, phase);
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, line, (SceSize) length);
        sceIoClose(fd);
    }
}

static void vitaRenderLog(const char* text) {
    extern int g_vitaProbeLoggingEnabled;
    if (!g_vitaProbeLoggingEnabled || text == NULL) return;
    SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                          SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, text, (SceSize) strlen(text));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
}

typedef struct Vita2DVertex {
    GLfloat u, v;
    GLfloat r, g, b, a;
    GLfloat x, y;
} Vita2DVertex;

// Fixed-function client arrays do not feed attributes of a glUseProgram
// shader in VitaGL. The Experimental shaders bind position/UV/colour to
// 0/1/2, so select the matching generic-array path while one is active.
static bool vitaCustomShaderActive = false;

static void vitaEnable2DArrays(const Vita2DVertex* vertices) {
    if (vitaCustomShaderActive) {
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vita2DVertex), &vertices[0].x);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vita2DVertex), &vertices[0].u);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vita2DVertex), &vertices[0].r);
    } else {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].u);
        glColorPointer(4, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].r);
        glVertexPointer(2, GL_FLOAT, sizeof(Vita2DVertex), &vertices[0].x);
    }
}

static void vitaDisable2DArrays(void) {
    if (vitaCustomShaderActive) {
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(0);
    } else {
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
}

// VitaGL's immediate-mode compatibility layer generates a large fixed-function
// shader at the first glEnd(). Use explicit client arrays on Vita instead. This
// is the primitive used by the Vita renderer migration; desktop/PS3 keep the
// original immediate-mode path below.
static void vitaDrawQuad(const Vita2DVertex vertices[4]) {
    static bool firstDraw = true;
    if (firstDraw) vitaRenderLog("VITA_ARRAY=first_draw_begin");
    vitaEnable2DArrays(vertices);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    if (firstDraw) {
        vitaRenderLog("VITA_ARRAY=first_draw_complete");
        firstDraw = false;
    }
    vitaDisable2DArrays();
}

#define VITA_IMMEDIATE_MAX_VERTICES 262144
static Vita2DVertex vitaImmediateVertices[VITA_IMMEDIATE_MAX_VERTICES];
static GLsizei vitaImmediateCount;
static GLenum vitaImmediateMode;
static GLfloat vitaImmediateU, vitaImmediateV;
static GLfloat vitaImmediateR = 1.0f, vitaImmediateG = 1.0f;
static GLfloat vitaImmediateB = 1.0f, vitaImmediateA = 1.0f;

static void vitaBegin(GLenum mode) {
    vitaImmediateMode = mode;
    vitaImmediateCount = 0;
}

static void vitaColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    vitaImmediateR = r; vitaImmediateG = g; vitaImmediateB = b; vitaImmediateA = a;
}

static void vitaColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    vitaColor4f((GLfloat) r / 255.0f, (GLfloat) g / 255.0f,
                (GLfloat) b / 255.0f, (GLfloat) a / 255.0f);
}

static void vitaTexCoord2f(GLfloat u, GLfloat v) {
    vitaImmediateU = u; vitaImmediateV = v;
}

static void vitaVertex2f(GLfloat x, GLfloat y) {
    if (vitaImmediateCount >= VITA_IMMEDIATE_MAX_VERTICES) return;
    vitaImmediateVertices[vitaImmediateCount++] = (Vita2DVertex) {
        vitaImmediateU, vitaImmediateV,
        vitaImmediateR, vitaImmediateG, vitaImmediateB, vitaImmediateA,
        x, y
    };
}

static void vitaDrawVertexRange(GLenum mode, const Vita2DVertex* vertices, GLsizei count) {
    vitaEnable2DArrays(vertices);
    glDrawArrays(mode, 0, count);
    vitaDisable2DArrays();
}

static void vitaEnd(void) {
    static bool firstConvertedBatch = true;
    if (firstConvertedBatch) vitaRenderLog("VITA_IMMEDIATE_BRIDGE=first_batch_begin");
    
    if (vitaImmediateMode == GL_QUADS) {
        static GLushort quadIndices[65536 * 6 / 4];
        static bool indicesInited = false;
        if (!indicesInited) {
            for (int i = 0, v = 0; i < (65536 * 6 / 4); i += 6, v += 4) {
                quadIndices[i+0] = v + 0;
                quadIndices[i+1] = v + 1;
                quadIndices[i+2] = v + 2;
                quadIndices[i+3] = v + 0;
                quadIndices[i+4] = v + 2;
                quadIndices[i+5] = v + 3;
            }
            indicesInited = true;
        }

        // Draw in chunks of 65536 vertices (16-bit index limit)
        for (GLsizei offset = 0; offset + 3 < vitaImmediateCount; offset += 65536) {
            GLsizei count = vitaImmediateCount - offset;
            if (count > 65536) count = 65536;
            GLsizei quads = count / 4;
            
            vitaEnable2DArrays(&vitaImmediateVertices[offset]);
            glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_SHORT, quadIndices);
            vitaDisable2DArrays();
        }
    } else if (vitaImmediateMode == GL_TRIANGLES) {
        vitaDrawVertexRange(GL_TRIANGLES, vitaImmediateVertices, vitaImmediateCount);
    }
    
    if (firstConvertedBatch) {
        vitaRenderLog("VITA_IMMEDIATE_BRIDGE=first_batch_complete");
        firstConvertedBatch = false;
    }
    vitaImmediateCount = 0;
}

// Convert every remaining immediate-mode call in this translation unit. This
// covers text, tiled sprites, primitives and surface blits while the dedicated
// Vita renderer is progressively split out of the legacy backend.
#define glBegin(mode) vitaBegin(mode)
#define glEnd() vitaEnd()
#define glColor4f(r, g, b, a) vitaColor4f((r), (g), (b), (a))
#define glColor4ub(r, g, b, a) vitaColor4ub((r), (g), (b), (a))
#define glTexCoord2f(u, v) vitaTexCoord2f((u), (v))
#define glVertex2f(x, y) vitaVertex2f((x), (y))
#endif
#include "matrix_math.h"
#include "text_utils.h"


#ifdef PLATFORM_PS3
#include "ps3gl.h"
#include "rsxutil.h"
#include "ps3_textures.h"
extern GLuint gPalettedProgram;
extern GLint  gPalettedUPaletteVLoc;
// Activate the paletted shader for a sprite draw. The caller has already bound the index texture (via glBindTexture on TEXUNIT0).
// Sets unit 1 to the CLUT atlas and pushes uPaletteV for the TPAG's row.
#define PS3_PALETTED_BEGIN(tpagIndex) do {                                                  \
    float _v = PS3Textures_getTpagPaletteV(tpagIndex);                                      \
    if (0.0f > _v) break;                                                                   \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glBindTexture(GL_TEXTURE_2D, PS3Textures_getClutTexture());                             \
    glEnable(GL_TEXTURE_2D);                                                                \
    glActiveTexture(GL_TEXTURE0);                                                           \
    glUseProgram(gPalettedProgram);                                                        \
    if (gPalettedUPaletteVLoc >= 0) glUniform1f(gPalettedUPaletteVLoc, _v);               \
} while (0)
#define PS3_PALETTED_END() do {                                                             \
    glUseProgram(0);                                                                        \
    glActiveTexture(GL_TEXTURE1);                                                           \
    glDisable(GL_TEXTURE_2D);                                                               \
    glActiveTexture(GL_TEXTURE0);                                                           \
} while (0)
#elif defined(PLATFORM_VITA)
#include <vitaGL.h>
#define PS3_PALETTED_BEGIN(tpagIndex) ((void)0)
#define PS3_PALETTED_END()            ((void)0)
#else
#include <glad/glad.h>
#define PS3_PALETTED_BEGIN(tpagIndex) ((void)0)
#define PS3_PALETTED_END()            ((void)0)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "math_compat.h"

// Next power-of-two, used for FBO texture dimensions on older GPUs (Intel 82865G etc.)
// that cannot attach NPOT textures to framebuffer objects.
static inline int32_t nextPow2(int32_t v) {
    int32_t r = 1;
    while (r < v) r <<= 1;
    return r;
}

// Checks whether an OpenGL extension is available. Uses the modern
// (glGetStringi + GL_NUM_EXTENSIONS) path when glGetStringi is non-null
// (GL 3.0+), otherwise falls back to the legacy glGetString(GL_EXTENSIONS)
// approach so the code works with any GL loader (glad, PS3, etc.).
#if !defined(PLATFORM_PS3) && !defined(PLATFORM_VITA)
static bool hasGLExtension(const char* name) {
    if (glGetStringi) {
        GLint numExts = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExts);
        for (GLint i = 0; i < numExts; i++) {
            const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (ext && strcmp(ext, name) == 0)
                return true;
        }
        return false;
    }
    const char* extStr = (const char*)glGetString(GL_EXTENSIONS);
    if (!extStr) return false;
    size_t len = strlen(name);
    for (const char* p = extStr; (p = strstr(p, name)) != NULL; p++) {
        if ((p == extStr || p[-1] == ' ') && (p[len] == ' ' || p[len] == '\0'))
            return true;
    }
    return false;
}
#endif

#ifdef PLATFORM_VITA
// VitaGL supports NPOT textures and the framebuffer functionality used by this renderer.
static bool hasGLExtension(const char* name) {
    (void)name;
    return true;
}
#endif

#include "stb_image.h"
#include "stb_ds.h"
#include "utils.h"
#include "image_decoder.h"
#include "gl_common.h"

// ===[ Runtime OpenGL extension checks ]===

static bool hasFBO() {
#if defined(PLATFORM_PS3) || defined(PLATFORM_VITA)
    return true;
#else
    return (glGenFramebuffers || (glGenFramebuffersEXT && glBlitFramebufferEXT));
#endif
}

#include "gl_wrappers.h"

// ===[ Helpers ]===

static void glApplyViewport(GLLegacyRenderer* gl, int32_t x, int32_t y, int32_t w, int32_t h) {
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);

    gl->base.CPortX = x;
    gl->base.CPortY = y;
    gl->base.CPortW = w;
    gl->base.CPortH = h;
}

static void glLegacyUpdateShaderMatrices(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*)renderer;
    if (renderer->currentShader < 0 || !gl->shaderPrograms ||
        (uint32_t)renderer->currentShader >= renderer->dataWin->shdr.count) return;
    GLuint program = gl->shaderPrograms[renderer->currentShader];
    if (!program) return;
    GLint matrices = glGetUniformLocation(program,
#ifdef PLATFORM_VITA
        "gm_Matrices"
#else
        "gm_Matrices[0]"
#endif
    );
    if (matrices >= 0) {
        Matrix4f m[MATRICES_MAX];
        memcpy(m, renderer->gmlMatrices, sizeof(m));
        Matrix4f_flipClipY(&m[MATRIX_PROJECTION]);
        Matrix4f_flipClipY(&m[MATRIX_WORLD_VIEW_PROJECTION]);
        glUniformMatrix4fv(matrices, MATRICES_MAX, GL_FALSE, m[0].m);
    }
    GLint compact = glGetUniformLocation(program, "u_vitaMVP");
    if (compact >= 0) {
        Matrix4f m = renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION];
        Matrix4f_flipClipY(&m);
        glUniformMatrix4fv(compact, 1, GL_FALSE, m.m);
    }
}

// camera_apply: swap the active world->clip projection on the current target without touching its viewport.
static void glApplyProjection(Renderer* renderer, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix) {

    Matrix4f world = renderer->gmlMatrices[MATRIX_WORLD];
    Matrix4f view = *viewMatrix;
    Matrix4f projection = *projectionMatrix;

    Matrix4f worldView;
    Matrix4f_multiply(&worldView, &view, &world);

    Matrix4f worldViewProjection;
    Matrix4f_multiply(&worldViewProjection, &projection, &worldView);
  
    renderer->gmlMatrices[MATRIX_VIEW] = view;   
    renderer->gmlMatrices[MATRIX_PROJECTION] = projection;
    renderer->gmlMatrices[MATRIX_WORLD_VIEW] = worldView;   
    renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = worldViewProjection;

    Matrix4f_flipClipY(&projection);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(worldView.m);
    // surface_set_target/reset_target and camera_apply do not call shader_set
    // again. Both fixed-function and programmable projections must follow them.
    glLegacyUpdateShaderMatrices(renderer);
}

// ===[ Vtable Implementations ]===

#ifdef PLATFORM_VITA
static uint32_t glCompileAllShaders(GLLegacyRenderer* gl, VitaShaderPrepareProgress progress, void* user);
#else
static void glCompileAllShaders(GLLegacyRenderer* gl);
#endif

static void glInit(Renderer* renderer, DataWin* dataWin) {
    renderer->currentShader = -1; // fixed pipeline until shader_set selects a program
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    renderer->dataWin = dataWin;

    Matrix4f world;
    Matrix4f_identity(&world);
    renderer->gmlMatrices[MATRIX_WORLD] = world;

    if (!hasFBO()) {
        fprintf(stderr, "GL: The legacy-gl renderer requires FBO support!\n");
        abort();
    }

    // GL 2.0+ has NPOT textures as core; older GL (1.x) may or may not have
    // GL_ARB_texture_non_power_of_two. Only round up to power-of-two on GPUs
    // that actually need it (Intel 82865G etc.).
    {
#if defined(PLATFORM_PS3) || defined(PLATFORM_VITA)
        // Both console backends support NPOT textures. VitaGL reports a
        // legacy-style GL version/extension set, which made this fallback
        // round every GML surface up to powers of two. Chapter 5's five town
        // surfaces then consumed tens of MiB beyond their logged logical size
        // and filled the 96 MiB VRAM pool while loading room_town_south.
        gl->needsPOT = false;
#else
        GLVer ver = GLCommon_getGLVersion();
        gl->needsPOT = (ver.major < 2) && !hasGLExtension("GL_ARB_texture_non_power_of_two");
#endif
    }

    // Prepare texture slots for lazy loading (PNG decode deferred to first use)
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

#ifdef PLATFORM_PS3
    // TXTR is empty on PS3; page count comes from TEXTURES.BIN.
    gl->textureCount = PS3Textures_getPageCount();
#else
    gl->textureCount = dataWin->txtr.count;
    // Record the immutable data.win range before allocating Vita-only page
    // metadata.  The old order allocated textureLastEvictedFrame with a zero
    // count and later indexed it using real page IDs.
    gl->originalTexturePageCount = gl->textureCount;
    // Compile GameMaker shaders ahead of time so the legacy renderer can honour
    // shader_set() (e.g. scr_pal_swap_set on the Swatchling enemies). Without
    // this the fixed pipeline ignores palette-swap shaders and sprites keep
    // their default palette.
    // Vita legacy-gl deliberately keeps the fixed-function pipeline active in
    // glGpuSetShader(). Compiling every embedded shader here therefore has no
    // rendering benefit and can exhaust/fragment GXM memory before Chapter 5
    // reaches the loading screen. Desktop legacy-gl still keeps its existing
    // compatibility path.
    if (dataWin->shdr.count > 0) {
        gl->shaderPrograms = (GLuint*)safeCalloc(dataWin->shdr.count, sizeof(GLuint));
        gl->shaderAttempted = (bool*)safeCalloc(dataWin->shdr.count, sizeof(bool));
        // Vita compiles only the compact, audited 2D compatibility set.  The
        // full generated GameMaker table is deliberately deferred because its
        // large vertex programs fragment GXM memory before Chapter 5 starts.
        // The selected set is prepared by the visible loading stage when the
        // user enables Shaders in Game Settings.
#ifdef PLATFORM_VITA
        extern int g_vitaShadersEnabled;
        extern void VitaProbe_logLine(const char* text);
        VitaProbe_logLine(g_vitaShadersEnabled ?
                          "SHADER_BOOT=enabled preload_deferred" :
                          "SHADER_BOOT=disabled fixed_pipeline");
#else
        glCompileAllShaders(gl);
#endif
    }
#endif
    gl->glTextures = (GLuint *)safeMalloc(gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeMalloc(gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeMalloc(gl->textureCount * sizeof(bool));
    gl->textureLastUsedFrame = (uint32_t *)safeCalloc(gl->textureCount, sizeof(uint32_t));
#ifdef PLATFORM_VITA
    gl->textureLastEvictedFrame = (uint32_t*)safeCalloc(gl->originalTexturePageCount,
                                                       sizeof(uint32_t));
#endif
    gl->texturePinned = (bool *)safeCalloc(gl->textureCount, sizeof(bool));
    gl->textureFrame = 1;
    gl->residentTextureBytes = 0;

    glGenTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->textureCount > i; i++) {
        gl->textureWidths[i] = 0;
        gl->textureHeights[i] = 0;
        gl->textureLoaded[i] = false;
    }

    // Create 1x1 white pixel texture for primitive drawing (rectangles, lines, etc.)
    glGenTextures(1, &gl->whiteTexture);
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);
    uint8_t whitePixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Save original counts so we know which slots are from data.win vs dynamic.
    // originalTexturePageCount is assigned above before Vita page metadata is
    // allocated; retain this assignment for the PS3 path.
#ifdef PLATFORM_PS3
    gl->originalTexturePageCount = gl->textureCount;
#endif
    gl->originalTpagCount = dataWin->tpag.count;
    gl->originalSpriteCount = dataWin->sprt.count;
#ifdef PLATFORM_VITA
    gl->textureRoomRequired = (bool*)safeCalloc(gl->originalTexturePageCount, sizeof(bool));
    gl->textureRoomPreload = (bool*)safeCalloc(gl->originalTexturePageCount, sizeof(bool));
    gl->textureGpuBytes = (uint64_t*)safeCalloc(gl->originalTexturePageCount, sizeof(uint64_t));
    gl->textureCacheValidated = (bool*)safeCalloc(gl->originalTexturePageCount, sizeof(bool));
    gl->textureRetryAfterFrame = (uint32_t*)safeCalloc(gl->originalTexturePageCount, sizeof(uint32_t));
    gl->textureRetryDelayFrames = (uint16_t*)safeCalloc(gl->originalTexturePageCount, sizeof(uint16_t));
    gl->textureCameraClass = (uint8_t*)safeCalloc(gl->originalTexturePageCount, sizeof(uint8_t));
    gl->textureCameraClassFrame = (uint32_t*)safeCalloc(gl->originalTexturePageCount, sizeof(uint32_t));
    // Keep the primary DELTARUNE font/UI atlas resident. Chapter 5 uses a full
    // 2048x2048 page for it, so size-based UI protection did not recognize it.
    bool pinnedMainFont = false;
    for (uint32_t i = 0; i < dataWin->font.count; ++i) {
        Font* font = &dataWin->font.fonts[i];
        if (!font->present || font->tpagIndex < 0 ||
            (uint32_t)font->tpagIndex >= dataWin->tpag.count) continue;
        if (font->name != nullptr && strstr(font->name, "fnt_main") != nullptr) {
            int page = dataWin->tpag.items[font->tpagIndex].texturePageId;
            if (page >= 0 && (uint32_t)page < gl->textureCount) {
                gl->texturePinned[page] = true;
                pinnedMainFont = true;
            }
        }
    }
    if (!pinnedMainFont && dataWin->font.count > 0) {
        Font* font = &dataWin->font.fonts[0];
        if (font->tpagIndex >= 0 && (uint32_t)font->tpagIndex < dataWin->tpag.count) {
            int page = dataWin->tpag.items[font->tpagIndex].texturePageId;
            if (page >= 0 && (uint32_t)page < gl->textureCount) gl->texturePinned[page] = true;
        }
    }
    // Do not pin every spr_textbox_* page globally. Across DELTARUNE those
    // sprites share several gameplay atlases; pinning all of them made almost
    // the entire Chapter 5 town working set immutable. The normal eight-frame
    // residency window is sufficient while a dialogue is being drawn, and the
    // room manifest protects static UI that belongs to the incoming room.
    // The castle-town upper/lower scenery and its adjoining rooms dynamically
    // select pages 10-12. They are absent from portions of the static room
    // manifest, so transition cleanup used to recycle them and make the town
    // change appearance after each doorway. At 2 MiB PVRTC2 each, retaining
    // this reviewed core costs only 6 MiB.
    extern int g_vitaActiveChapter;
    if (g_vitaActiveChapter == 2) {
        for (uint32_t page = 10; page <= 12 && page < gl->textureCount; ++page)
            gl->texturePinned[page] = true;

        // Pin only the small functional set which is selected dynamically and
        // is therefore absent from static room manifests. Keeping pages 20-25
        // globally fixed solved NPC churn, but consumed enough of the 104 MiB
        // budget to defer the battle cursor (3), battle effects (4) and castle
        // restaurant art (19). Ordinary NPC/character pages now rely on the
        // longer Chapter 2 working-set window below and can rotate by region.
        const uint32_t dynamicPages[] = {3U, 4U, 19U};
        for (uint32_t i = 0; i < sizeof(dynamicPages) / sizeof(dynamicPages[0]); ++i) {
            uint32_t page = dynamicPages[i];
            if (page < gl->textureCount) gl->texturePinned[page] = true;
        }
    }
    // The playable Kris directions are assigned dynamically by GML, so they
    // are absent from the static room preload manifest. Under Chapter 5 town
    // pressure their atlas could be requested only after every scenery page
    // had already been touched in that frame, leaving no legal LRU victim and
    // making obj_mainchara draw with a missing texture. Keep only the four
    // normal directional sprite families resident, not cutscene Kris assets.
    for (uint32_t i = 0; i < dataWin->sprt.count; ++i) {
        Sprite* sprite = &dataWin->sprt.sprites[i];
        const char* name = sprite->name;
        if (!sprite->present || name == nullptr || sprite->tpagIndices == nullptr) continue;
        // Match only the four ordinary walking sprites. Prefix matching also
        // caught slide, cutscene and alternate Kris families and pinned many
        // unrelated 2048px pages. In the Chapter 5 town trace that accidental
        // protected set occupied about 60 MiB, so the school (page 42), Kris
        // and dialogue pages could never enter the 64 MiB cache.
        bool krisDirection = strcmp(name, "spr_krisd") == 0 ||
                             strcmp(name, "spr_krisl") == 0 ||
                             strcmp(name, "spr_krisr") == 0 ||
                             strcmp(name, "spr_krisu") == 0;
        if (!krisDirection) continue;
        for (uint32_t frame = 0; frame < sprite->textureCount; ++frame) {
            int32_t tpagIndex = sprite->tpagIndices[frame];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dataWin->tpag.count) continue;
            int page = dataWin->tpag.items[tpagIndex].texturePageId;
            if (page >= 0 && (uint32_t)page < gl->textureCount)
                gl->texturePinned[page] = true;
        }
    }
#endif

 #ifdef PLATFORM_VITA
    // Phase 7 support: pin DEDICATED small (<=512) font/UI pages so the relaxed
    // small-page LRU (no longer immortal) can never evict glyph/textbox atlases
    // and make text disappear. Large shared atlases that merely happen to hold a
    // textbox frame are intentionally NOT pinned here, to avoid freezing the
    // working set (the documented Chapter 5 town over-pin problem).
    for (uint32_t p = 0; p < gl->textureCount; ++p) {
        if (gl->texturePinned[p] || !vitaSemanticFontOrUiPage(dataWin, p)) continue;
        int stw = p < dataWin->txtr.count ? dataWin->txtr.textures[p].textureWidth : 0;
        int sth = p < dataWin->txtr.count ? dataWin->txtr.textures[p].textureHeight : 0;
        if (stw > 0 && sth > 0 && stw <= 512 && sth <= 512)
            gl->texturePinned[p] = true;
    }

    // Phase 6 classification telemetry. Logs every page the semantic detector
    // (vitaSemanticFontOrUiPage) marks as font/UI, every page vitaPixelCriticalPage
    // marks critical, and every page the legacy hardcoded Chapter 2 IDs would
    // mark, flagging divergences (DIFF). Lets us confirm on-device that the
    // semantic set covers the legacy set before the IDs are removed.
    {
        extern int g_vitaProbeLoggingEnabled;
        extern int g_vitaActiveChapter;
        if (g_vitaProbeLoggingEnabled) {
            for (uint32_t p = 0; p < gl->textureCount; ++p) {
                bool sem = vitaSemanticFontOrUiPage(dataWin, p);
                bool crit = vitaPixelCriticalPage(dataWin, p);
                bool legacyCh2 = g_vitaActiveChapter == 2 &&
                    (p == 12U || p == 14U || p == 16U || p == 17U ||
                     p == 24U || p == 25U || p == 26U || p == 29U);
                if (!sem && !crit && !legacyCh2) continue;
                int tw = p < dataWin->txtr.count ? dataWin->txtr.textures[p].textureWidth : 0;
                int th = p < dataWin->txtr.count ? dataWin->txtr.textures[p].textureHeight : 0;
                char phase[80];
                snprintf(phase, sizeof(phase),
                         "classify font_ui=%d critical=%d legacy_ch2=%d%s pinned=%d",
                         sem ? 1 : 0, crit ? 1 : 0, legacyCh2 ? 1 : 0,
                         (g_vitaActiveChapter == 2 && sem != legacyCh2) ? " DIFF" : "",
                         gl->texturePinned[p] ? 1 : 0);
                vitaTextureLog(p, tw, th, 0, phase);
            }
        }
    }
#endif

    // application_surface is allocated lazily by glLegacyEnsureApplicationSurface as a normal entry in the surface table.
    gl->surfaces = nullptr;
    gl->surfaceTexture = nullptr;
    gl->surfaceWidth = nullptr;
    gl->surfaceHeight = nullptr;
    gl->surfaceCount = 0;

    fprintf(stderr, "GL: Renderer initialized (%u texture pages)\n", gl->textureCount);
}

static void glDestroy(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glDeleteTextures(1, &gl->whiteTexture);

    glDeleteTextures((GLsizei) gl->textureCount, gl->glTextures);

    for (uint32_t i = 0; gl->surfaceCount > i; i++) {
        if (gl->surfaceTexture[i] != 0) glDeleteTextures(1, &gl->surfaceTexture[i]);
        if (gl->surfaces[i] != 0) glDeleteFramebuffers(1, &gl->surfaces[i]);
    }
    free(gl->surfaces);
    free(gl->surfaceTexture);
    free(gl->surfaceWidth);
    free(gl->surfaceHeight);

    if (gl->shaderPrograms) {
        DataWin* dw = gl->base.dataWin;
        for (uint32_t i = 0; dw && i < dw->shdr.count; i++) {
            if (gl->shaderPrograms[i]) glDeleteProgram(gl->shaderPrograms[i]);
        }
        free(gl->shaderPrograms);
    }
    free(gl->shaderAttempted);

    free(gl->glTextures);
    free(gl->textureWidths);
    free(gl->textureHeights);
    free(gl->textureLoaded);
    free(gl->textureLastUsedFrame);
#ifdef PLATFORM_VITA
    free(gl->textureLastEvictedFrame);
#endif
    free(gl->texturePinned);
#ifdef PLATFORM_VITA
    // The persistent cache staging block belongs to this renderer lifetime.
    // Finish pending transfers before returning its USER_RW memory.
    if (vitaTextureStagingBlock >= 0) {
        glFinish();
        sceKernelFreeMemBlock(vitaTextureStagingBlock);
        vitaTextureStagingBlock = -1;
        vitaTextureStagingBase = nullptr;
        vitaTextureStagingBytes = 0;
    }
    for (int i = 0; i < VITA_CPU_TEXTURE_CACHE_SLOTS; ++i) free(gl->cpuTextureCachePixels[i]);
    for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i)
        if (gl->retiredTextures[i] != 0) glDeleteTextures(1, &gl->retiredTextures[i]);
    free(gl->textureRoomRequired);
    free(gl->textureRoomPreload);
    free(gl->textureGpuBytes);
    free(gl->textureCacheValidated);
    free(gl->textureRetryAfterFrame);
    free(gl->textureRetryDelayFrames);
    free(gl->textureCameraClass);
    free(gl->textureCameraClassFrame);
#endif
    free(gl);
}

static void glBeginFrame(Renderer* renderer, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    gl->textureFrame++;
    if (gl->textureFrame == 0) gl->textureFrame = 1;
#ifdef PLATFORM_VITA
    // Screen-filter changes are live. Update only atlas textures that are
    // already resident; future uploads receive the same policy normally.
    // This avoids a chapter restart, cache regeneration or texture reupload.
    if (gl->appliedTextureFilterRevision != g_vitaTextureFilterRevision) {
        GLenum filter = vitaTextureFilter();
        for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
            if (!gl->textureLoaded[i] || gl->textureWidths[i] <= 0 ||
                gl->glTextures[i] == 0) continue;
            glBindTexture(GL_TEXTURE_2D, gl->glTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        gl->appliedTextureFilterRevision = g_vitaTextureFilterRevision;
    }
    // Phase 5: the Texture Compression mode was changed in Game Settings.
    // Release every resident, non-pinned page so it lazily reloads in the newly
    // selected format (BC3 <-> RGBA4444). Pinned font/UI pages are always
    // RGBA4444 in every mode and are left intact. Stored .r444/.pvr files are
    // never deleted; only the GPU-resident copies are dropped.
    if (g_vitaTextureCacheInvalidate) {
        g_vitaTextureCacheInvalidate = 0;
        for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i)
            vitaEvictSpecificTexturePage(gl, i);
        vitaTextureLog(0, 0, 0, 0, "texture_cache_invalidated_config_change");
    }
#endif
#ifdef PLATFORM_VITA
    bool collectedRetiredTexture = false;
    bool hasMatureRetiredTexture = false;
    for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
        if (gl->retiredTextures[i] != 0 &&
            gl->textureFrame - gl->retiredTextureFrames[i] >= 4U) {
            hasMatureRetiredTexture = true;
            break;
        }
    }
    // The four-frame age prevents reuse while recent GXM lists may reference
    // the texture, but it does not itself guarantee that the GPU has finished
    // those lists. Collecting the single-threaded VitaGL heap without this
    // fence blocked the Chapter 5 loop immediately after entering town_south.
    if (hasMatureRetiredTexture) glFinish();
    for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
        if (gl->retiredTextures[i] != 0 &&
            gl->textureFrame - gl->retiredTextureFrames[i] >= 4U) {
            glDeleteTextures(1, &gl->retiredTextures[i]);
            gl->retiredTextures[i] = 0;
            gl->retiredTextureFrames[i] = 0;
            gl->retiredTextureBytes[i] = 0;
            collectedRetiredTexture = true;
        }
    }
    // The VitaGL build uses single-threaded garbage collection. glDeleteTextures
    // only retires the backing allocation; without an explicit collection the
    // tracked atlas set can shrink while the physical VRAM pool remains full.
    // Collect once after the four-frame GXM safety window, never per texture.
    if (collectedRetiredTexture) vglForceGarbageCollection();
#endif

    gl->windowW = windowW;
    gl->windowH = windowH;
    gl->gameW = gameW;
    gl->gameH = gameH;

    // Bind the application_surface (sized/created by Runner_beginFrame's ensureApplicationSurface call right before this).
    int32_t appId = gl->base.runner->applicationSurfaceId;
    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[appId]);
    glViewport(0, 0, gameW, gameH);
    gl->base.CPortX = 0;
    gl->base.CPortY = 0;
    gl->base.CPortW = gameW;
    gl->base.CPortH = gameH;
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void glBeginView(Renderer* renderer, MAYBE_UNUSED int32_t viewX, MAYBE_UNUSED int32_t viewY, MAYBE_UNUSED int32_t viewW, MAYBE_UNUSED int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, MAYBE_UNUSED float viewAngle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    // Set viewport and scissor to the port rectangle within the FBO
    // FBO uses game resolution, port coordinates are in game space
    // OpenGL viewport Y is bottom-up, game Y is top-down
    glApplyViewport(gl, portX, portY, portW, portH);

    int32_t viewCurrent = 0;
    if (renderer->runner->viewsEnabled) {
    viewCurrent = renderer->runner->viewCurrent;
    }
    RuntimeView* view = &renderer->runner->views[viewCurrent];
    gl->base.cameraCurrent = view->cameraId;
    GMLCamera* camera = Runner_getCameraById(renderer->runner, gl->base.cameraCurrent);
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);

    glActiveTexture(GL_TEXTURE0);

}

static void glEndView(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glBeginGUI(Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    glBindTexture(GL_TEXTURE_2D, 0);

    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
#ifdef PLATFORM_VITA
        extern int g_vitaPortOverlayFullScreen;
        if (!g_vitaPortOverlayFullScreen) {
            int32_t sx, sy, ex, ey;
            GLCommon_computeLetterbox(gl->gameW, gl->gameH, gl->windowW, gl->windowH, &sx, &sy, &ex, &ey);
            glViewport(sx, gl->windowH - ey, ex - sx, ey - sy);
            glEnable(GL_SCISSOR_TEST);
            glScissor(sx, gl->windowH - ey, ex - sx, ey - sy);
        } else {
            glViewport(0, 0, portW, portH);
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, portW, portH);
        }
#else
        glViewport(0, 0, portW, portH);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, portW, portH);
#endif
    } else {
        require(targetSurfaceId >= 0 && (uint32_t) targetSurfaceId < gl->surfaceCount);
        require(gl->surfaces[targetSurfaceId] != 0);
        glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[targetSurfaceId]);
        glApplyViewport(gl, portX, portY, portW, portH);
    }

    //I dunno hopefully this is at least somewhat correct...
    gl->base.cameraCurrent = GUI_CAMERA;
    GMLCamera* camera = &renderer->runner->guiCamera;
    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = guiW;
    camera->viewHeight = guiH;
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;

    Matrix4f projectionMatrix;
    Matrix4f_Orthographic(&projectionMatrix, (float) guiW, (float) guiH, 32000.0, 0.0);

    Matrix4f viewMatrix;
    float x = (float) guiW * 0.5f;
    float y = (float) guiH * 0.5f;
    Matrix4f_identity(&viewMatrix);
    Matrix4f_LookAt(&viewMatrix, x, y, -16000.0, x, y, 16000.0, 0.0, 1.0, 0.0);
    camera->viewMatrix = viewMatrix;
    camera->projectionMatrix = projectionMatrix;
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);

    glActiveTexture(GL_TEXTURE0);
}

static void glSetGuiProjection(MAYBE_UNUSED Renderer* renderer, int32_t guiW, int32_t guiH, int32_t portW, int32_t portH, bool renderingToUserSurface) {
    Matrix4f projection;
    Matrix4f_guiProjection(&projection, (float) guiW, (float) guiH, (float) portW, (float) portH);
    // GL surfaces are stored bottom-up and draw_surface samples them with vertical flip.

    renderer->cameraCurrent = GUI_CAMERA;
    GMLCamera* camera = &renderer->runner->guiCamera;
    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = guiW;
    camera->viewHeight = guiH;
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;

    //yeah no I have no idea how to do the GUI
    Matrix4f projectionMatrix;
    Matrix4f_Orthographic(&projectionMatrix, (float) guiW, (float) guiH, 32000.0, 0.0);
    // Flip the projection when we are rendering to a user surface so it comes back upright.
    if (renderingToUserSurface) Matrix4f_flipClipY(&projectionMatrix);
    Matrix4f viewMatrix;
    float x = (float) guiW * 0.5f;
    float y = (float) guiH * 0.5f;
    Matrix4f_identity(&viewMatrix);
    Matrix4f_LookAt(&viewMatrix, x, y, -16000.0, x, y, 16000.0, 0.0, 1.0, 0.0);
    camera->viewMatrix = viewMatrix;
    camera->projectionMatrix = projectionMatrix;
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
}

static void glEndGUI(MAYBE_UNUSED Renderer* renderer) {
    glDisable(GL_SCISSOR_TEST);
}

static void glEndFrameInit(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], 0);
}

static void glEndFrameEnd(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (renderer->runner->usingAppSurface && !renderer->runner->appSurfaceAutoDraw) {
        return;
    }
    int32_t appId = gl->base.runner->applicationSurfaceId;
    GLCommon_beginLetterboxBlit(gl->surfaces[appId], 0);
    GLCommon_endLetterboxBlit(gl->surfaceWidth[appId], gl->surfaceHeight[appId], gl->gameW, gl->gameH, gl->windowW, gl->windowH, 0);
}

static void glRendererFlush(MAYBE_UNUSED Renderer* renderer) {}

static void glClearScreen(MAYBE_UNUSED Renderer* renderer, uint32_t color, float alpha) {
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // GML draw_clear ignores the active scissor and clears the whole target. Disable scissor for the clear and restore it after.

    glClearColor(r, g, b, alpha);
    glClear(GL_COLOR_BUFFER_BIT);

}

// Lazily decodes and uploads a TXTR page on first access.
// Returns true if the texture is ready, false if it failed to decode.
#ifdef PLATFORM_VITA
// Chapter 2's castle scene actively alternates five 2048x2048 pages (80 MiB)
// plus smaller pages. Keep that working set, but leave roughly 16 MiB of the
// 112 MiB graphics pool for framebuffers, VitaGL bookkeeping and console
// borders. A 104 MiB texture limit left under 8 MiB free and Chapter 5 crashed
// while replacing a 2048x2048 atlas at about 101 MiB resident.
static uint64_t vitaTextureCacheLimit(void) {
    extern int g_vitaActiveChapter;
    // Surfaces, the console border and VitaGL bookkeeping are not included in
    // residentTextureBytes. The town_south save-load trace reached exactly
    // 96 MiB of VitaGL VRAM with only 49 MiB reported here, then crashed after
    // its five surfaces and first-frame pages were submitted. Keep another
    // eight MiB outside the tracked atlas budget for those untracked costs.
    // With deterministic collection of retired textures, Chapter 5 can keep
    // its measured 58-62 MiB working set instead of cycling at the old 44 MiB
    // emergency ceiling. The compressed 2048px scenery pages leave the same
    // headroom without sacrificing Original-quality dimensions.
    // With PVRTC2 enabled, most 2048px pages occupy 2 MiB. Town still needs
    // one or two policy-selected 8 MiB RGBA4444 pages for sensitive sprites
    // and fonts. The old 76 MiB ceiling rejected those pages at ~74 MiB even
    // while VitaGL reported 20-38 MiB free, producing permanent flicker and
    // missing actors. Admit that mixed working set but retain twelve MiB for
    // surfaces/borders. The all-RGBA profile keeps the conservative ceiling.
    // Flowery owns 34-37 render surfaces (about 22 MiB) outside the tracked
    // atlas counter. A late 8 MiB RGBA4444 upload previously passed the 88 MiB
    // atlas test with only 7.7 MiB of VitaGL VRAM left and crashed. Bound its
    // atlas working set independently from the rest of Chapter 5.
    if (g_vitaActiveChapter == 5 && vitaFloweryBattleMemoryMode)
        // Flowery's transient render surfaces use RGBA4444 on Vita. That
        // recovers roughly 13 MiB and lets the measured 17-19 page BC3
        // working set remain resident instead of re-uploading it every draw.
        // Keep the atlas side below the measured 96 MiB CDRAM ceiling. The
        // room owns another ~22 MiB in composition surfaces and a border/UI
        // reserve, so the old 76 MiB limit left only 2-3 MiB free and made
        // every new battle phase replace several active pages at once.
        return 68ULL * 1024ULL * 1024ULL;
    if (g_vitaActiveChapter == 5)
        return (g_vitaPvrEnabled ? 88ULL : 76ULL) * 1024ULL * 1024ULL;
    // Mansion rooms can create a 1920x480 surface and immediately copy it to
    // a dynamic sprite.  That transient pair needs about 7 MiB which is not
    // represented by residentTextureBytes.  The east_teacup -> east_2f_a
    // trace reached 103 MiB of atlases with only 6.5 MiB left in VitaGL and
    // crashed during that copy.  Keep a mansion-only reserve; Cyber/Castle
    // retain the larger working set required by their measured atlases.
    if (g_vitaActiveChapter == 2 && vitaCh2TexturePolicyRegion == 4)
        return 92ULL * 1024ULL * 1024ULL;
    // Chapter 2 page 25/26 must use the lossless RGBA4444 path because their
    // mixed character/UI atlases are corrupted by BC3. The mansion 4F trace
    // reaches 90 MiB and then retries one missing 8 MiB page forever under
    // the generic 96 MiB ceiling. A 104 MiB chapter-specific ceiling admits
    // that page while retaining at least 8 MiB of the 112 MiB VitaGL pool for
    // renderer bookkeeping and surfaces.
    // v0.68-3 kept Chapter 2's castle/city atlas set resident and consequently
    // rendered castle_area_2_transformed without missing pages or reload
    // stutter. Its global 192 MiB budget was unsafe for Chapter 5, but Chapter
    // 2 only needs one additional RGBA4444 page beyond the generic ceiling.
    if (g_vitaActiveChapter == 2)
        return ((vitaCh2CyberIntroMemoryMode || vitaCh2BattleMemoryMode) ?
                96ULL : 104ULL) * 1024ULL * 1024ULL;
    // Chapter 3 commonly keeps two large application/event surfaces alive
    // (about 12.8 MiB in the couch rooms).  Lossless-marker pages also occupy
    // 8 MiB each instead of the 2 MiB PVRTC representation.  The generic
    // 96 MiB atlas ceiling therefore allowed 83-92 MiB of atlases while
    // VitaGL had only ~11 MiB free, then the next room upload exhausted the
    // graphics pool.  Keep enough headroom for those surfaces and for one
    // incoming RGBA4444 page during a transition.
    // The camera-sized circle-zoom surface reduced the measured Chapter 3
    // surface set to 1.2-3.6 MiB. In BC3 mode the previous 76 MiB atlas cap
    // still evicted active couch backgrounds whenever the game menu loaded a
    // shared UI page, even though VitaGL reported roughly 30 MiB free. Keep
    // three more 4 MiB BC3 pages resident while retaining safe FBO headroom.
    if (g_vitaActiveChapter == 3) {
        // TV/gameshow cutscenes continuously introduce GML-selected atlas pages.
        // v0.72-35 reached ~81 MiB resident with only ~5 MiB left in the mapped
        // staging pools, after which STB could no longer decode embedded PNGs.
        // Keep extra headroom only for this sequence.
        if (vitaChapter3TvMemoryMode)
            return (g_vitaBc3OnlyEnabled ? 76ULL : 68ULL) * 1024ULL * 1024ULL;
        return (g_vitaBc3OnlyEnabled ? 88ULL : 76ULL) * 1024ULL * 1024ULL;
    }
    // VitaGL only has ~112MB of total memory pool. Asking for 192MB causes severe swap
    // stalls and FBO exhaustion in Chapter 3. Limit default to 96MB so textures are
    // properly evicted before hitting VitaGL's internal walls.
    return 96ULL * 1024ULL * 1024ULL;
}

// residentTextureBytes cannot see render targets, the console border or
// VitaGL's internal allocations. Flowery owns about 22 MiB of surfaces, so a
// fixed atlas ceiling alone still allowed the real pool to fall below 6 MiB
// and crash during the next compressed upload. Keep a measured reserve only
// in this battle; other rooms retain their established cache behaviour.
static bool vitaTextureUploadHasPhysicalHeadroom(uint64_t uploadBytes) {
    if (vitaFloweryBattleMemoryMode) {
        // The v0.77-14 Vita trace kept 12.4-15.9 MiB free while repeatedly
        // replacing 2 MiB 1024px pages. Requiring a full 12 MiB reserve for
        // those small uploads reduced the effective cache to about 34 MiB and
        // caused the tower/decoration atlases to alternate every few frames.
        // Eight MiB still blocks a risky 8 MiB atlas at that pressure, while
        // allowing two more 2 MiB battle pages to remain resident.
        const uint64_t reserve = 8ULL * 1024ULL * 1024ULL;
        return (uint64_t)vglMemFree(VGL_MEM_VRAM) >= uploadBytes + reserve;
    }
    if (vitaChapter3TvMemoryMode) {
        // Complete R444/BC3 sets are generated before gameplay, so ordinary
        // TV-room uploads only need space for the encoded page plus a small
        // driver margin. The former fixed 8 MiB staging reserve repeatedly
        // evicted active 1024x1024 pages while 4-6 MiB was still available,
        // producing visible atlas flicker on the RectPOT build. Large R444
        // pages still request their full payload here and retain more reserve.
        uint64_t vramFree = (uint64_t)vglMemFree(VGL_MEM_VRAM);
        uint64_t stagingFree = (uint64_t)vglMemFree(VGL_MEM_RAM) +
                               (uint64_t)vglMemFree(VGL_MEM_SLOW);
        const uint64_t vramReserve = 10ULL * 1024ULL * 1024ULL;
        uint64_t stagingReserve = uploadBytes + 1ULL * 1024ULL * 1024ULL;
        if (stagingReserve < 2ULL * 1024ULL * 1024ULL)
            stagingReserve = 2ULL * 1024ULL * 1024ULL;
        return vramFree >= uploadBytes + vramReserve && stagingFree >= stagingReserve;
    }
    return true;
}

static uint16_t* vitaCpuTextureCacheGet(GLLegacyRenderer* gl, uint32_t pageId, int* w, int* h) {
    for (int i = 0; i < VITA_CPU_TEXTURE_CACHE_SLOTS; ++i) {
        if (gl->cpuTextureCachePixels[i] != nullptr && gl->cpuTextureCachePage[i] == pageId) {
            gl->vitaTextureRamHits++;
            gl->cpuTextureCacheStamp[i] = ++gl->cpuTextureCacheClock;
            *w = gl->cpuTextureCacheWidth[i];
            *h = gl->cpuTextureCacheHeight[i];
            return gl->cpuTextureCachePixels[i];
        }
    }
    return nullptr;
}

static uint16_t* vitaCpuTextureCacheStore(GLLegacyRenderer* gl, uint32_t pageId,
                                          uint16_t* pixels, int w, int h) {
    // A full 2048x2048 RGBA4444 page costs 8 MiB. The mansion acid-loop trace
    // had only ~461 KiB free in the VitaGL RAM pool while this cache retained
    // such a page, amplifying every failed GPU upload. Upload directly and
    // release the staging allocation in severe rooms.
    // Chapters 3-5 use many 2048x2048 pages in rapid succession. Retaining
    // one decoded RGBA4444 page here consumes another 8 MiB of the same heap
    // needed to stage the next page. The Chapter 3 TV traces consequently
    // reported existing pages as missing after malloc failed. Keep disk cache
    // as the authoritative cache and release staging pixels after each GPU
    // upload in these chapters.
    if (gl->vitaSevereRoomTextures || vitaTextureChapter(gl->base.dataWin) >= 3)
        return pixels;
    int slot = -1;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < VITA_CPU_TEXTURE_CACHE_SLOTS; ++i) {
        if (gl->cpuTextureCachePixels[i] == nullptr) { slot = i; break; }
        if (gl->cpuTextureCacheStamp[i] < oldest) {
            oldest = gl->cpuTextureCacheStamp[i];
            slot = i;
        }
    }
    if (slot < 0) return pixels;
    free(gl->cpuTextureCachePixels[slot]);
    gl->cpuTextureCachePixels[slot] = pixels;
    gl->cpuTextureCachePage[slot] = pageId;
    gl->cpuTextureCacheWidth[slot] = w;
    gl->cpuTextureCacheHeight[slot] = h;
    gl->cpuTextureCacheStamp[slot] = ++gl->cpuTextureCacheClock;
    return pixels;
}

static bool vitaCpuTextureCacheOwns(const GLLegacyRenderer* gl, const uint16_t* pixels) {
    if (pixels == nullptr) return false;
    for (int i = 0; i < VITA_CPU_TEXTURE_CACHE_SLOTS; ++i)
        if (gl->cpuTextureCachePixels[i] == pixels) return true;
    return false;
}

static void vitaReleasePreparedPixels(GLLegacyRenderer* gl, uint16_t* pixels) {
    if (pixels == vitaTextureStagingBase && vitaTextureStagingBlock >= 0) {
        // VitaGL can finish consuming client memory after glTexImage2D
        // returns. Fence before allowing the persistent block to be reused;
        // freeing/reallocating it here caused corruption on room revisits.
        glFinish();
        return;
    }
    if (!vitaCpuTextureCacheOwns(gl, pixels)) free(pixels);
}

static bool vitaTextureNeedsFullColor(GLLegacyRenderer* gl, uint32_t pageId) {
    // Non-zero alpha rounding keeps fonts and spr_darkconfigbt visible in
    // RGBA4444, avoiding a 16 MiB full-color atlas in Chapter 2.
    (void)gl;
    (void)pageId;
    return false;
}

static bool vitaTexturePageHasFont(GLLegacyRenderer* gl, uint32_t pageId) {
    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < dw->font.count; ++i) {
        Font* font = &dw->font.fonts[i];
        if (!font->present || font->tpagIndex < 0 || (uint32_t)font->tpagIndex >= dw->tpag.count) continue;
        if ((uint32_t)dw->tpag.items[font->tpagIndex].texturePageId == pageId) return true;
    }
    return false;
}

static uint64_t vitaGpuTextureBytes(GLLegacyRenderer* gl, uint32_t pageId, int w, int h) {
    return (uint64_t)w * (uint64_t)h *
           (vitaTextureNeedsFullColor(gl, pageId) ? 4ULL : 2ULL);
}

static bool vitaEvictTexturePage(GLLegacyRenderer* gl, uint32_t protectedPage) {
    extern int g_vitaActiveChapter;
    uint32_t victim = UINT32_MAX;
    uint32_t oldestFrame = UINT32_MAX;
    bool urgentRequest = false;
    if (protectedPage < gl->originalTexturePageCount) {
        bool visibleThisFrame = gl->textureCameraClass != nullptr &&
            gl->textureCameraClassFrame != nullptr &&
            gl->textureCameraClassFrame[protectedPage] == gl->textureFrame &&
            gl->textureCameraClass[protectedPage] == 2U;
        bool staticRoomAsset = gl->textureRoomPreload != nullptr &&
            gl->textureRoomPreload[protectedPage];
        urgentRequest = visibleThisFrame || staticRoomAsset;
    }
    for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
        if (i == protectedPage || gl->texturePinned[i] || !gl->textureLoaded[i] || gl->textureWidths[i] <= 0 || gl->textureHeights[i] <= 0) continue;
        // The translated Chapter 2 layout has one 4096x4096 BC3 animation
        // atlas (page 28, 16 MiB). Replacing it in the urgent fallback every
        // few frames costs 0.3-3.5 seconds per upload and is the reason battles
        // are slower only with PT-BR enabled. Keep it while it remains part of
        // the recent battle working set; room-change trimming can still retire
        // it after leaving the region.
        if (g_vitaActiveChapter == 2 && i == 28U &&
            vitaTextureCacheVariant(gl->base.dataWin)[0] != '\0' &&
            gl->textureFrame - gl->textureLastUsedFrame[i] <= 600U)
            continue;
        // Phase 7: small atlases (fonts, dialogue frames, menu sprites) are no
        // longer immortal. Genuine glyph/UI pages are protected via texturePinned
        // (semantic pinning); ordinary small pages now participate in the LRU at
        // LOW priority (see the protectedAge bonus below), so hundreds of small
        // pages can no longer pin the whole session.
        bool smallPage = gl->textureWidths[i] <= 512 && gl->textureHeights[i] <= 512;
        // A page used by an animation may not be touched on every single
        // frame. Evicting it after only one idle frame made Chapter 5 alternate
        // pages 49/54 continuously, which appeared as missing or unrelated
        // sprites. Preserve a short rolling working set; genuinely stale room
        // pages remain eligible after eight frames.
        uint32_t age = gl->textureFrame - gl->textureLastUsedFrame[i];
        // Flowery's late phases repeatedly replaced the same 1024px pages
        // (page 108 alone was evicted 48 times in one capture). Once a page
        // has paid the synchronous upload cost, keep it stable for one second
        // so another stale phase page is selected instead. Room transitions
        // use the explicit trim path and are unaffected.
        if (vitaFloweryBattleMemoryMode && gl->textureLastEvictedFrame != nullptr &&
            gl->textureLastEvictedFrame[i] != 0 &&
            gl->textureFrame - gl->textureLastEvictedFrame[i] <= 30U)
            continue;
        // Camera visibility belongs to an individual quad, not to its atlas.
        // A single 2048 page can contain distant scenery, the visible player,
        // dialogue and UI simultaneously. Treating the first off-camera quad
        // as proof that the whole page was stale allowed that shared atlas to
        // be evicted before a later visible quad used it in the same frame.
        // Keep camera classification for draw/load decisions, but use the
        // conservative animation window for every resident atlas.
        // Chapter 2 frequently changes an NPC's sprite through GML and may
        // leave a shared atlas untouched for more than eight frames between
        // animation phases. Retain its active working set for two seconds;
        // room-transition trimming still discards genuinely old-region pages
        // at the 104 MiB ceiling.
        // Chapter 2 dynamically swaps sprites long after Room Start.  The
        // constrained-room flag controls camera read-ahead, not whether an
        // atlas remains part of the active animation set.  Using only eight
        // frames here caused castle_area_2_transformed and intro_connector to
        // enter a permanent BC3 evict/upload cycle after idling.  Preserve the
        // measured two-second working set in every Chapter 2 room; explicit
        // transition trimming still removes pages from the previous region.
        // Flowery changes phases abruptly and each phase has a different set
        // of large atlases. Retaining the previous phase for eight frames
        // consumed the complete CDRAM pool before the new thorns/Idea assets
        // could enter. Explicitly promoted core pages remain protected by the
        // room manifest; ordinary phase pages can retire after two frames.
        uint32_t protectedAge = (g_vitaActiveChapter == 2) ? 120U :
                                (g_vitaActiveChapter == 3 ? 30U :
                                 (vitaFloweryBattleMemoryMode ? 2U : 8U));
        // Phase 7: small pages are low-priority, not immortal. They only become
        // eligible after idling far longer than large scenery, so they are
        // reclaimed as a last resort instead of accumulating forever.
        // Small pages are normally retained as a cheap last-resort cache.
        // Flowery is the exception: every battle phase introduces another
        // group of 128/256px effect pages, and keeping all of them for 30
        // seconds fragmented VitaGL until the next background upload failed.
        // The explicitly promoted UI/effect pages remain pinned by the room
        // manifest, so ordinary small phase pages only need a short grace.
        if (smallPage) {
            // RectPOT intentionally creates many 64-512px pages. Keeping all
            // of them protected for 30 seconds accumulated old cutscene art,
            // while the LRU recycled the currently visible 1024px TV pages.
            // Semantic UI/font pages are pinned separately, so ordinary small
            // Chapter 3 atlases only need a short animation grace period.
            if (g_vitaActiveChapter == 3)
                protectedAge += vitaChapter3TvMemoryMode ? 120U : 300U;
            else
                protectedAge += vitaFloweryBattleMemoryMode ? 30U : 1800U;
        }
        if (age <= protectedAge) continue;
        if (gl->textureLastUsedFrame[i] < oldestFrame) {
            oldestFrame = gl->textureLastUsedFrame[i];
            victim = i;
        }
    }
    // A visible sprite or a static destination background must win over the
    // short animation-age window. Previously all recently used pages were
    // protected, so Chapter 5 repeatedly deferred pages 29/49/50/58 for
    // hundreds of frames while stale room art remained resident. Prefer a
    // page not referenced by the destination, then fall back to the oldest
    // non-pinned large atlas if the whole resident set is shared.
    if (victim == UINT32_MAX && urgentRequest) {
        for (int pass = 0; pass < 2 && victim == UINT32_MAX; ++pass) {
            oldestFrame = UINT32_MAX;
            for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
                if (i == protectedPage || gl->texturePinned[i] || !gl->textureLoaded[i]) continue;
                bool smallPage = gl->textureWidths[i] <= 512 && gl->textureHeights[i] <= 512;
                uint32_t age = gl->textureFrame - gl->textureLastUsedFrame[i];
                // Prefer replacing a large stale page first. If every large
                // page belongs to the active scene, the second pass may reclaim
                // an old RectPOT page instead of making visible art disappear.
                if (smallPage && (pass == 0 || age <= 120U)) continue;
                if (g_vitaActiveChapter == 2 && i == 28U &&
                    vitaTextureCacheVariant(gl->base.dataWin)[0] != '\0' &&
                    gl->textureFrame - gl->textureLastUsedFrame[i] <= 600U)
                    continue;
                // A page evicted and requested again within two seconds is a
                // member of the active working set.  Chapter 2 battles were
                // cycling pages 2/5/21 every frame: each upload blocked Draw
                // for 1.6-1.8 seconds and starved the audio stream.  Prefer a
                // genuinely stale atlas; if none exists, defer the requesting
                // page rather than enter that destructive replacement loop.
                if (gl->textureLastEvictedFrame != nullptr &&
                    gl->textureLastEvictedFrame[i] != 0 &&
                    gl->textureFrame - gl->textureLastEvictedFrame[i] <= 120U)
                    continue;
                if (pass == 0 && gl->textureRoomRequired != nullptr &&
                    gl->textureRoomRequired[i]) continue;
                if (gl->textureLastUsedFrame[i] < oldestFrame) {
                    oldestFrame = gl->textureLastUsedFrame[i];
                    victim = i;
                }
            }
        }
    }
    if (victim == UINT32_MAX) return false;

    uint64_t bytes = gl->textureGpuBytes != nullptr && gl->textureGpuBytes[victim] != 0 ?
                     gl->textureGpuBytes[victim] :
                     vitaGpuTextureBytes(gl, victim, gl->textureWidths[victim], gl->textureHeights[victim]);
    vitaTextureLog(victim, gl->textureWidths[victim], gl->textureHeights[victim],
                   4096, "evict_lru");
    gl->vitaTextureEvictions++;
    glBindTexture(GL_TEXTURE_2D, 0);
    GLuint oldTexture = gl->glTextures[victim];
    bool retired = false;
    if (!urgentRequest) {
        for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
            if (gl->retiredTextures[i] == 0) {
                gl->retiredTextures[i] = oldTexture;
                gl->retiredTextureFrames[i] = gl->textureFrame;
                gl->retiredTextureBytes[i] = bytes;
                retired = true;
                break;
            }
        }
    }
    if (!retired) {
        // Urgent replacement must actually release its allocation before the
        // new upload. Deferring deletion for four frames kept up to 32 MiB of
        // invisible atlases alive and made the admission check meaningless.
        // Urgent eviction used to delete/reuse the GL texture name while
        // previously submitted Vita GXM draws still referenced it. Under the
        // PT-BR atlas pressure this rendered earlier objects with a completely
        // different page (or only corrupted animation frames). Fence before
        // reusing the allocation; non-urgent victims remain in the retired
        // queue and do not need this synchronous path.
        glFinish();
        glDeleteTextures(1, &oldTexture);
        vglForceGarbageCollection();
    }
    glGenTextures(1, &gl->glTextures[victim]);
    gl->textureLoaded[victim] = false;
    gl->textureWidths[victim] = 0;
    gl->textureHeights[victim] = 0;
    gl->textureLastUsedFrame[victim] = 0;
    if (gl->textureLastEvictedFrame != nullptr)
        gl->textureLastEvictedFrame[victim] = gl->textureFrame;
    if (gl->textureGpuBytes != nullptr) gl->textureGpuBytes[victim] = 0;
    gl->residentTextureBytes = gl->residentTextureBytes > bytes ? gl->residentTextureBytes - bytes : 0;
    return true;
}

// Room transitions already know which pages belong to the incoming static
// scene. Evict the selected stale page itself instead of feeding it back to
// the runtime LRU as a protected page (the old code consequently removed an
// unrelated destination page and made rooms change appearance on revisits).
static bool vitaEvictSpecificTexturePage(GLLegacyRenderer* gl, uint32_t victim) {
    if (gl == nullptr || victim >= gl->originalTexturePageCount ||
        gl->texturePinned[victim] || !gl->textureLoaded[victim] ||
        gl->textureWidths[victim] <= 0 || gl->textureHeights[victim] <= 0)
        return false;

    uint64_t bytes = gl->textureGpuBytes != nullptr && gl->textureGpuBytes[victim] != 0 ?
                     gl->textureGpuBytes[victim] :
                     vitaGpuTextureBytes(gl, victim, gl->textureWidths[victim],
                                         gl->textureHeights[victim]);
    vitaTextureLog(victim, gl->textureWidths[victim], gl->textureHeights[victim],
                   4096, "evict_room_stale");
    gl->vitaTextureEvictions++;
    glBindTexture(GL_TEXTURE_2D, 0);
    GLuint oldTexture = gl->glTextures[victim];
    // This path runs between rooms, before the destination starts uploading
    // its working set.  Retiring up to four stale 8 MiB RGBA4444 pages made
    // residentTextureBytes claim that 32 MiB was free while VitaGL still held
    // it.  Chapter 3 then crashed immediately after the first destination PVR
    // upload.  There is no useful frame to preserve here: finish the previous
    // room and reclaim the allocation before admitting destination textures.
    glFinish();
    glDeleteTextures(1, &oldTexture);
    vglForceGarbageCollection();
    glGenTextures(1, &gl->glTextures[victim]);
    gl->textureLoaded[victim] = false;
    gl->textureWidths[victim] = 0;
    gl->textureHeights[victim] = 0;
    gl->textureLastUsedFrame[victim] = 0;
    if (gl->textureLastEvictedFrame != nullptr)
        gl->textureLastEvictedFrame[victim] = gl->textureFrame;
    if (gl->textureGpuBytes != nullptr) gl->textureGpuBytes[victim] = 0;
    gl->residentTextureBytes = gl->residentTextureBytes > bytes ?
                               gl->residentTextureBytes - bytes : 0;
    return true;
}
#endif

bool GLLegacyRenderer_ensureTextureLoaded(GLLegacyRenderer* gl, uint32_t pageId) {
    if (gl->textureLoaded[pageId]) {
        if (pageId < gl->originalTexturePageCount) gl->textureLastUsedFrame[pageId] = gl->textureFrame;
        return (gl->textureWidths[pageId] != 0);
    }
#ifdef PLATFORM_VITA
    // Check the progressive retry deadline before claiming this page. The old
    // ordering set textureLoaded=true first; a call during the backoff window
    // could then leave a zero-sized page permanently marked as loaded.
    if (gl->textureRetryAfterFrame != nullptr &&
        gl->textureFrame < gl->textureRetryAfterFrame[pageId]) return false;
    // Once the protected working set fills the cache, do not decode every
    // remaining missing atlas only to discover the same condition again.
    bool importantRequest = pageId < gl->originalTexturePageCount &&
        ((gl->textureCameraClass != nullptr && gl->textureCameraClassFrame != nullptr &&
          gl->textureCameraClassFrame[pageId] == gl->textureFrame &&
          gl->textureCameraClass[pageId] == 2U) ||
         (gl->textureRoomPreload != nullptr && gl->textureRoomPreload[pageId]));
    if (gl->textureCacheBlockedFrame == gl->textureFrame && !importantRequest) return false;
#endif

    gl->textureLoaded[pageId] = true;

    int w, h;
#ifdef PLATFORM_PS3
    // We'll load the textures on demand.
    uint8_t* pixels;
    if (!PS3Textures_loadPage(pageId, &w, &h, &pixels)) {
        fprintf(stderr, "GL: PS3 page %u has no pixels\n", pageId);
        return false;
    }
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    // Nearest is mandatory for index textures, bilinear would interpolate palette indices into nonsense colors.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    free(pixels);
#else
    DataWin* dw = gl->base.dataWin;
    Texture* txtr = &dw->txtr.textures[pageId];
    uint8_t* pixels = nullptr;
    uint16_t* preparedPixels = nullptr;
#ifdef PLATFORM_VITA
    // Reject an impossible upload before reading an 8 MiB PVR/R444 page or
    // decoding the embedded PNG. In room_dw_castle_area_2_transformed pages
    // 10/12/24 were known to be over budget, yet every retry still performed
    // the full read/conversion and blocked rendering for 230-570 ms. The
    // dimensions are available in TXTR, so reserve cache space first.
    int sourceW = txtr->textureWidth;
    int sourceH = txtr->textureHeight;
    if (sourceW > 0 && sourceH > 0) {
        uint64_t encodedPvrBytes = vitaPvrTextureEncodedBytes(dw, pageId);
        uint64_t preflightBytes = encodedPvrBytes != 0 ? encodedPvrBytes :
                                  vitaGpuTextureBytes(gl, pageId, sourceW, sourceH);
        uint64_t preflightLimit = vitaTextureCacheLimit();
        while (gl->residentTextureBytes + preflightBytes > preflightLimit ||
               !vitaTextureUploadHasPhysicalHeadroom(preflightBytes)) {
            if (!vitaEvictTexturePage(gl, pageId)) break;
        }
        if (gl->residentTextureBytes + preflightBytes > preflightLimit ||
            !vitaTextureUploadHasPhysicalHeadroom(preflightBytes)) {
            gl->vitaTextureDeferred++;
            if (gl->textureRetryDelayFrames != nullptr) {
                uint16_t previous = gl->textureRetryDelayFrames[pageId];
                uint16_t delay = gl->texturePinned[pageId] ? 1U :
                                 previous == 0 ? 3U :
                                 previous <= 3U ? 15U :
                                 previous <= 15U ? 60U : 180U;
                gl->textureRetryDelayFrames[pageId] = delay;
                gl->textureRetryAfterFrame[pageId] = gl->textureFrame + delay;
                char retryPhase[96];
                snprintf(retryPhase, sizeof(retryPhase), "preflight_deferred_%u_next_%u",
                         (unsigned int)delay,
                         (unsigned int)gl->textureRetryAfterFrame[pageId]);
                vitaTextureLog(pageId, sourceW, sourceH, 4096, retryPhase);
            }
            gl->textureCacheBlockedFrame = gl->textureFrame;
            gl->textureLoaded[pageId] = false;
            return false;
        }
    }
    uint8_t* pvrPayload = nullptr;
    uint32_t pvrPayloadSize = 0;
    int pvrW = 0, pvrH = 0;
    uint64_t pvrFormat = 0;
    // PVR is authoritative whenever present. All accepted pages are native
    // PVRTC2 and can be uploaded without decoding or CPU-side conversion.
    if (vitaReadPvr(dw, pageId, &pvrPayload, &pvrPayloadSize, &pvrW, &pvrH, &pvrFormat)) {
        uint64_t cacheLimit = vitaTextureCacheLimit();
        while (gl->residentTextureBytes + pvrPayloadSize > cacheLimit ||
               !vitaTextureUploadHasPhysicalHeadroom(pvrPayloadSize)) {
            if (!vitaEvictTexturePage(gl, pageId)) break;
        }
        if (gl->residentTextureBytes + pvrPayloadSize <= cacheLimit &&
            vitaTextureUploadHasPhysicalHeadroom(pvrPayloadSize)) {
            gl->textureWidths[pageId] = pvrW;
            gl->textureHeights[pageId] = pvrH;
            glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
            while (glGetError() != GL_NO_ERROR) {}
            GLenum compressedFormat = pvrFormat == VITA_PVR_BC3_DXT5_FORMAT ?
                GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG;
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, compressedFormat,
                pvrW, pvrH, 0, (GLsizei)pvrPayloadSize, pvrPayload);
            GLenum pvrError = glGetError();
            // Compressed uploads are asynchronous in VitaGL as well. Fence
            // before freeing/reusing the source or later atlases can overwrite
            // sprites still being copied by GXM.
            vitaReleaseTexturePayload(pvrPayload, true);
            if (pvrError == GL_NO_ERROR) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, vitaTextureFilter());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, vitaTextureFilter());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                gl->residentTextureBytes += pvrPayloadSize;
                gl->textureGpuBytes[pageId] = pvrPayloadSize;
                gl->textureLastUsedFrame[pageId] = gl->textureFrame;
                vitaTextureLog(pageId, pvrW, pvrH, 4096,
                    pvrFormat == VITA_PVR_BC3_DXT5_FORMAT ?
                    "pvr_bc3_dxt5_uploaded" : "pvr_pvrtc2_4bpp_uploaded");
                return true;
            }
            vitaTextureLog(pageId, pvrW, pvrH, 4096, "pvr_upload_failed");
        } else {
            vitaReleaseTexturePayload(pvrPayload, false);
            gl->textureLoaded[pageId] = false;
            gl->vitaTextureDeferred++;
            if (gl->textureRetryDelayFrames != nullptr) {
                uint16_t previous = gl->textureRetryDelayFrames[pageId];
                uint16_t delay = gl->texturePinned[pageId] ? 1U :
                                 previous == 0 ? 3U :
                                 previous <= 3U ? 15U :
                                 previous <= 15U ? 60U : 180U;
                gl->textureRetryDelayFrames[pageId] = delay;
                gl->textureRetryAfterFrame[pageId] = gl->textureFrame + delay;
            }
            return false;
        }
    }
    preparedPixels = vitaCpuTextureCacheGet(gl, pageId, &w, &h);
    if (preparedPixels == nullptr) {
        preparedPixels = vitaLoadPreparedTexture(dw, txtr, pageId, &w, &h);
        if (preparedPixels != nullptr) {
            vitaCpuTextureCacheStore(gl, pageId, preparedPixels, w, h);
        }
    }
#endif
#ifdef PLATFORM_VITA
    if (preparedPixels == nullptr) {
        if (vitaTextureChapter(dw) == 0) {
            // The launcher data.win is version-stamped during preparation,
            // which changes TXTR blob offsets and invalidates caches produced
            // from the unmodified Steam file. It has only a few pages, so a
            // one-time embedded decode is both safe and more robust than
            // requiring regenerated launcher assets for every build label.
            vitaTextureLog(pageId, txtr->textureWidth, txtr->textureHeight, 4096,
                           "chapter0_embedded_fallback");
        } else {
            // Runtime decoding from data.win caused large temporary
            // allocations and non-deterministic results in gameplay chapters.
            vitaTextureLog(pageId, txtr->textureWidth, txtr->textureHeight, 4096,
                           "missing_pvr_and_texture_cache");
            if (txtr->textureWidth <= 0 || txtr->textureHeight <= 0) {
                gl->textureLoaded[pageId] = true;
                gl->textureWidths[pageId] = 0;
                gl->textureHeights[pageId] = 0;
            } else {
                gl->textureLoaded[pageId] = false;
                if (gl->textureRetryAfterFrame != nullptr)
                    gl->textureRetryAfterFrame[pageId] = gl->textureFrame + 180U;
            }
            return false;
        }
    }
#endif
    if (preparedPixels == nullptr)
    {
        DataWin_loadTxtrIfNeeded(dw, pageId);
        bool gm2022_5 = DataWin_isVersionAtLeast(dw, 2022, 5, 0, 0);
        pixels = ImageDecoder_decodeToRgba(txtr->blobData, (size_t) txtr->blobSize, gm2022_5, &w, &h);
        if (pixels == nullptr) {
            fprintf(stderr, "GL: Failed to decode TXTR page %u\n", pageId);
            return false;
        }
        if (!txtr->mapped) {
            free(txtr->blobData);
            txtr->blobData = nullptr;
        }
    }

    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;

#ifdef PLATFORM_VITA
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (w <= 0 || h <= 0 || w > maxTextureSize || h > maxTextureSize) {
        vitaTextureLog(pageId, w, h, maxTextureSize, "rejected_invalid_size");
        free(pixels);
        vitaReleasePreparedPixels(gl, preparedPixels);
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    // RGBA4444 cuts atlas residency and transfer size in half. DELTARUNE's
    // pixel-art presentation tolerates the 4-bit channels well, while keeping
    // the Chapter 2/5 working sets resident instead of decoding the same PNGs
    // every few frames.
    uint64_t pixelCount = (uint64_t)w * (uint64_t)h;
    bool fullColor = vitaTextureNeedsFullColor(gl, pageId);
    uint64_t uploadBytes = (uint64_t)w * (uint64_t)h * (fullColor ? 4ULL : 2ULL);
    uint64_t cacheLimit = vitaTextureCacheLimit();
    while (gl->residentTextureBytes + uploadBytes > cacheLimit ||
           !vitaTextureUploadHasPhysicalHeadroom(uploadBytes)) {
        if (!vitaEvictTexturePage(gl, pageId)) break;
    }
    if (gl->residentTextureBytes + uploadBytes > cacheLimit ||
        !vitaTextureUploadHasPhysicalHeadroom(uploadBytes)) {
        // Every possible victim was already used by this frame. The old path
        // uploaded anyway, temporarily exceeding the 112 MiB graphics pool;
        // Chapter 5 could enqueue many such pages in one room change and crash.
        // Defer this atlas until the next frame, when an older page can be
        // reclaimed safely. This also prevents a 20-30 second unbroken upload
        // burst while leaving currently referenced textures valid.
        gl->vitaTextureDeferred++;
        if (gl->textureRetryDelayFrames != nullptr) {
            uint16_t previous = gl->textureRetryDelayFrames[pageId];
            uint16_t delay = gl->texturePinned[pageId] ? 1U :
                             previous == 0 ? 3U :
                             previous <= 3U ? 15U :
                             previous <= 15U ? 60U : 180U;
            gl->textureRetryDelayFrames[pageId] = delay;
            gl->textureRetryAfterFrame[pageId] = gl->textureFrame + delay;
            char retryPhase[96];
            snprintf(retryPhase, sizeof(retryPhase), "deferred_retry_%u_next_%u",
                     (unsigned int)delay,
                     (unsigned int)gl->textureRetryAfterFrame[pageId]);
            vitaTextureLog(pageId, w, h, 4096, retryPhase);
        }
        free(pixels);
        vitaReleasePreparedPixels(gl, preparedPixels);
        gl->textureCacheBlockedFrame = gl->textureFrame;
        gl->textureLoaded[pageId] = false;
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    uint16_t* packedPixels = preparedPixels;
    uint16_t* gpuPackedPixels = nullptr;
    if (!fullColor) {
        // Pack into the first half of the decoded RGBA allocation. Reading four
        // bytes and writing two bytes advances the destination more slowly than
        // the source, so this is safe in-place and avoids an extra 8 MiB staging
        // allocation for every 2048x2048 atlas. The old 24 MiB peak per upload
        // fragmented memory during Chapter 5's large room changes.
        if (packedPixels == nullptr) {
            packedPixels = (uint16_t*)pixels;
            for (uint64_t i = 0; i < pixelCount; ++i) {
                const uint8_t* src = &pixels[i * 4ULL];
                uint16_t alpha4 = src[3] == 0 ? 0 : (uint16_t)((src[3] + 15U) >> 4);
                if (alpha4 > 15U) alpha4 = 15U;
                packedPixels[i] = (uint16_t)(((uint16_t)(src[0] >> 4) << 12) |
                                             ((uint16_t)(src[1] >> 4) << 8) |
                                             ((uint16_t)(src[2] >> 4) << 4) |
                                             alpha4);
            }
            vitaSavePreparedTexture(dw, txtr, pageId, w, h, packedPixels);
            vitaTextureLog(pageId, w, h, maxTextureSize, "prepared_cache_written");
        }
        gpuPackedPixels = packedPixels;
    } else {
        vitaReleasePreparedPixels(gl, preparedPixels);
        preparedPixels = nullptr;
    }
#endif
    glBindTexture(GL_TEXTURE_2D, gl->glTextures[pageId]);
#ifdef PLATFORM_VITA
    while (glGetError() != GL_NO_ERROR) {}
#endif
#ifdef PLATFORM_VITA
    if (fullColor) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);
        pixels = nullptr;
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, gpuPackedPixels);
        if (gpuPackedPixels != packedPixels) free(gpuPackedPixels);
        if (pixels != nullptr) free(pixels);
        else vitaReleasePreparedPixels(gl, packedPixels);
        pixels = nullptr;
        packedPixels = nullptr;
        gpuPackedPixels = nullptr;
    }
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
#endif

#ifdef PLATFORM_VITA
    GLenum uploadError = glGetError();
    if (uploadError != GL_NO_ERROR) {
        vitaTextureLog(pageId, w, h, maxTextureSize, "upload_failed_gpu_memory");
        free(pixels);
        glDeleteTextures(1, &gl->glTextures[pageId]);
        glGenTextures(1, &gl->glTextures[pageId]);
        gl->textureLoaded[pageId] = false;
        gl->textureWidths[pageId] = 0;
        gl->textureHeights[pageId] = 0;
        return false;
    }
    gl->residentTextureBytes += uploadBytes;
    if (gl->textureRetryDelayFrames != nullptr) {
        gl->textureRetryDelayFrames[pageId] = 0;
        gl->textureRetryAfterFrame[pageId] = 0;
    }
    if (gl->textureGpuBytes != nullptr) gl->textureGpuBytes[pageId] = uploadBytes;
    gl->textureLastUsedFrame[pageId] = gl->textureFrame;
#endif

    free(pixels);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, vitaTextureFilter());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, vitaTextureFilter());
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
#endif
    fprintf(stderr, "GL: Loaded TXTR page %u (%dx%d)\n", pageId, w, h);
    return true;
}
#ifdef PLATFORM_VITA
static void vitaMarkBackgroundPage(GLLegacyRenderer* gl, int32_t bgndIndex) {
    if (gl == nullptr || bgndIndex < 0 || gl->textureRoomRequired == nullptr) return;
    DataWin* dw = gl->base.dataWin;
    if (dw == nullptr || (uint32_t)bgndIndex >= dw->bgnd.count) return;
    int32_t tpagIndex = dw->bgnd.backgrounds[bgndIndex].tpagIndex;
    if (tpagIndex >= 0 && (uint32_t)tpagIndex < dw->tpag.count) {
        int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
        if (pageId >= 0 && (uint32_t)pageId < gl->originalTexturePageCount) {
            gl->textureRoomRequired[pageId] = true;
        }
    }
}

static void vitaMarkBackgroundPageByName(GLLegacyRenderer* gl, const char* name,
                                         bool preload) {
    if (gl == nullptr || name == nullptr || gl->base.dataWin == nullptr) return;
    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < dw->bgnd.count; ++i) {
        Background* bg = &dw->bgnd.backgrounds[i];
        if (!bg->present || bg->name == nullptr || strcmp(bg->name, name) != 0)
            continue;
        int32_t tpagIndex = bg->tpagIndex;
        if (tpagIndex >= 0 && (uint32_t)tpagIndex < dw->tpag.count) {
            int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
            if (pageId >= 0 && (uint32_t)pageId < gl->originalTexturePageCount) {
                gl->textureRoomRequired[pageId] = true;
                if (preload && gl->textureRoomPreload != nullptr)
                    gl->textureRoomPreload[pageId] = true;
            }
        }
        return;
    }
}

static void vitaMarkSpritePages(GLLegacyRenderer* gl, int32_t sprtIndex) {
    if (gl == nullptr || sprtIndex < 0 || gl->textureRoomRequired == nullptr) return;
    DataWin* dw = gl->base.dataWin;
    if (dw == nullptr || (uint32_t)sprtIndex >= dw->sprt.count) return;
    const Sprite* spr = &dw->sprt.sprites[sprtIndex];
    if (spr->tpagIndices != nullptr) {
        for (uint32_t i = 0; i < spr->textureCount; ++i) {
            int32_t tpagIndex = spr->tpagIndices[i];
            if (tpagIndex >= 0 && (uint32_t)tpagIndex < dw->tpag.count) {
                int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
                if (pageId >= 0 && (uint32_t)pageId < gl->originalTexturePageCount) {
                    gl->textureRoomRequired[pageId] = true;
                }
            }
        }
    }
}

static void vitaMarkSpritePagesByName(GLLegacyRenderer* gl, const char* name,
                                      bool preload) {
    if (gl == nullptr || name == nullptr || gl->base.dataWin == nullptr) return;
    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* spr = &dw->sprt.sprites[i];
        if (!spr->present || spr->name == nullptr || strcmp(spr->name, name) != 0)
            continue;
        vitaMarkSpritePages(gl, (int32_t)i);
        if (preload && gl->textureRoomPreload != nullptr && spr->tpagIndices != nullptr) {
            for (uint32_t frame = 0; frame < spr->textureCount; ++frame) {
                int32_t tpagIndex = spr->tpagIndices[frame];
                if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
                int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
                if (pageId >= 0 && (uint32_t)pageId < gl->originalTexturePageCount)
                    gl->textureRoomPreload[pageId] = true;
            }
        }
        return;
    }
}

static uint32_t vitaEnsureSpritePagesByName(GLLegacyRenderer* gl,
                                            const char* name) {
    if (gl == nullptr || name == nullptr || gl->base.dataWin == nullptr) return 0;
    DataWin* dw = gl->base.dataWin;
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* spr = &dw->sprt.sprites[i];
        if (!spr->present || spr->name == nullptr || strcmp(spr->name, name) != 0)
            continue;
        for (uint32_t frame = 0; frame < spr->textureCount; ++frame) {
            int32_t tpagIndex = spr->tpagIndices != nullptr ?
                                spr->tpagIndices[frame] : -1;
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
            if (pageId < 0 || (uint32_t)pageId >= gl->originalTexturePageCount)
                continue;
            bool wasLoaded = gl->textureLoaded[pageId];
            if (GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t)pageId) &&
                !wasLoaded) loaded++;
        }
        break;
    }
    return loaded;
}

static void vitaPinSpritePagesByName(GLLegacyRenderer* gl, const char* name) {
    if (gl == nullptr || name == nullptr || gl->base.dataWin == nullptr ||
        gl->texturePinned == nullptr) return;
    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* spr = &dw->sprt.sprites[i];
        if (!spr->present || spr->name == nullptr || strcmp(spr->name, name) != 0)
            continue;
        for (uint32_t frame = 0; frame < spr->textureCount; ++frame) {
            int32_t tpagIndex = spr->tpagIndices != nullptr ? spr->tpagIndices[frame] : -1;
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
            if (pageId >= 0 && (uint32_t)pageId < gl->originalTexturePageCount)
                gl->texturePinned[pageId] = true;
        }
        return;
    }
}

static uint32_t vitaEnsureSpritePagesByPrefix(GLLegacyRenderer* gl, const char* prefix) {
    if (gl == nullptr || prefix == nullptr || gl->base.dataWin == nullptr ||
        gl->texturePinned == nullptr) return 0;
    DataWin* dw = gl->base.dataWin;
    const size_t prefixLength = strlen(prefix);
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < dw->sprt.count; ++i) {
        const Sprite* spr = &dw->sprt.sprites[i];
        if (!spr->present || spr->name == nullptr ||
            strncmp(spr->name, prefix, prefixLength) != 0) continue;
        for (uint32_t frame = 0; frame < spr->textureCount; ++frame) {
            int32_t tpagIndex = spr->tpagIndices != nullptr ? spr->tpagIndices[frame] : -1;
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dw->tpag.count) continue;
            int16_t pageId = dw->tpag.items[tpagIndex].texturePageId;
            if (pageId < 0 || (uint32_t)pageId >= gl->originalTexturePageCount) continue;
            bool wasLoaded = gl->textureLoaded[pageId];
            if (GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t)pageId) && !wasLoaded)
                loaded++;
        }
    }
    return loaded;
}

uint32_t GLLegacyRenderer_preloadChapterBattleCore(GLLegacyRenderer* gl) {
    if (gl == nullptr || gl->base.dataWin == nullptr) return 0;
    extern int g_vitaActiveChapter;
    // The first battle in every chapter can upload the shared UI/party set on
    // its first combat draw. Chapter 2 logs measured several 0.6-1.4 second
    // render stalls while the resident set grew by roughly 21 MiB; subsequent
    // battle frames were smooth. Resolve the small common core during the
    // chapter loading screen instead. Names keep this valid after atlas
    // rebuilds and language-specific repacks.
    if (g_vitaActiveChapter <= 0) return 0;
    static const char* const battleCore[] = {
        "spr_battlebg_0", "spr_battlemsg", "spr_battleblcon",
        "spr_battlebox", "spr_battleborder", "spr_battlebar",
        "spr_attackspot", "spr_battletarget_triangle", "spr_heart",
        "spr_dodgeheart", "spr_krisb_attackready",
        "spr_susieb_attackready", "spr_ralseib_attackready",
        "spr_krisb_idle", "spr_susieb_idle", "spr_ralseib_idle",
        "spr_krisb_idle_unarmed", "spr_susieb_idle_unarmed",
        "spr_susieb_attackready_unarmed", "spr_susieb_attackready_unhappy",
        // First-use hit feedback can live outside the normal battle/UI pages
        // after the data.win atlas rebuild. Without these entries the first
        // successful attack or first player hit still uploads its page during
        // the damage frame even though the battle itself was pre-warmed.
        "spr_hpslash", "spr_numbersfontbig", "spr_numbersfontsmall",
        "spr_heartbreak", "spr_heartshards",
        // Common Chapter 1 & 2 encounters and UI elements
        "spr_tensionbar", "spr_btact", "spr_btfight", "spr_btitem",
        "spr_btspare", "spr_btdefend", "spr_bthalf", "spr_battleblcon_long",
        "spr_battleblcon_morelong", "spr_chara0", "spr_chara0_hand",
        "spr_charbt_heart", "spr_battle_menu_glow", "spr_tp_bubbles",
        "spr_diamondm_idle", "spr_diamondm_hurt", "spr_diamondm_spared",
        "spr_rabbick_enemy", "spr_rabbick_enemy_hurt", "spr_rabbick_enemy_spared",
        "spr_hathyx_idle", "spr_hathyx_hurt", "spr_hathyx_spared",
        "spr_jigsawry_idle", "spr_jigsawry_hurt", "spr_jigsawry_spared",
        "spr_smallchecker_idle", "spr_smallchecker_hurt",
        "spr_blockguy_part", "spr_blockguy_part_hurt"
    };
    uint32_t loaded = 0;
    for (uint32_t i = 0; i < sizeof(battleCore) / sizeof(battleCore[0]); ++i) {
#ifdef PLATFORM_VITA
        extern void VitaProbe_pumpLoadingScreen(void);
        VitaProbe_pumpLoadingScreen();
#endif
        loaded += vitaEnsureSpritePagesByName(gl, battleCore[i]);
        // Preload behind the chapter loading screen, but do not make battle
        // atlases immortal. Chapter 3 accumulated 93 pinned pages this way;
        // room_board_1 then had no legal victim and alternated pages 0/2 in a
        // 0.9-1.5 second upload loop until STB allocations failed.
    }
    // Do not preload whole name prefixes. In Chapter 3 those broad prefixes
    // resolve to 89 pages (roughly 72 MiB) before gameplay starts, including
    // attacks from unrelated battles. The exact shared UI/party core above is
    // enough to remove the first battle-box hitch; room-specific effects stay
    // lazy and remain eligible for normal LRU eviction.
    return loaded;
}

void GLLegacyRenderer_trimTextureCacheForRoomChange(GLLegacyRenderer* gl, uint64_t targetBytes, bool includeSmallPages) {
    if (gl == nullptr || gl->residentTextureBytes <= targetBytes) return;
    const bool purgeForNativeVideo = targetBytes == 0;
    for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
        if (gl->residentTextureBytes <= targetBytes) break;
        if (gl->texturePinned[i] || !gl->textureLoaded[i]) continue;
        // Severe rooms need their inherited 2048px pages recreated at
        // the 1024px room profile. Keeping them merely because the destination
        // also references them preserves the oversized allocation and leaves
        // no space for the remaining visible pages.
        // Native PVR pages are already in their final GPU representation and
        // Original quality never resamples an atlas. Recreating those pages on
        // every Chapter 5 Town doorway only discarded destination assets and
        // reloaded the identical bytes a few frames later.
        bool recreateRequiredLarge = false;
        // Only static destination art is guaranteed to be used immediately.
        // textureRoomRequired also includes the default sprite of every room
        // object, including controllers and objects that never draw their
        // default sprite. Preserving that conservative set kept 55 MiB of
        // stale school/castle pages in Chapter 5 and forced visible pages
        // 45/46/56/59 into a permanent upload/eviction loop.
        if (!purgeForNativeVideo && gl->textureRoomPreload != nullptr && gl->textureRoomPreload[i] &&
            !recreateRequiredLarge) continue;
        // Object sprites are added to textureRoomRequired after the static
        // preload manifest. Preserve that destination set while it fits inside
        // a bounded 8 MiB transition allowance; this avoids unloading one
        // shared NPC/UI atlas without allowing the conservative object manifest
        // to refill the whole Chapter 5 cache before the first visible draw.
        if (!purgeForNativeVideo && gl->textureRoomRequired != nullptr && gl->textureRoomRequired[i] &&
            !recreateRequiredLarge &&
            gl->residentTextureBytes <= targetBytes + 8ULL * 1024ULL * 1024ULL)
            continue;
        if (!purgeForNativeVideo && !includeSmallPages &&
            gl->textureWidths[i] <= 512 && gl->textureHeights[i] <= 512) continue;
        extern int g_vitaActiveChapter;
        if (g_vitaActiveChapter == 5 && (i == 30 || i == 55)) continue;
        vitaEvictSpecificTexturePage(gl, i);
    }
}

void GLLegacyRenderer_collectRetiredTexturesForRoomChange(GLLegacyRenderer* gl) {
    if (gl == nullptr) return;
    bool hasRetiredTextures = false;
    for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
        if (gl->retiredTextures[i] != 0) {
            hasRetiredTextures = true;
            break;
        }
    }
    if (!hasRetiredTextures) return;

    // The outgoing room has finished and the destination has not drawn yet,
    // so this is the safe point to fence its GXM list. Reclaim the backing
    // allocations before preloading replacements instead of retaining tens
    // of MiB for four additional frames.
    glFinish();
    for (int i = 0; i < VITA_RETIRED_TEXTURE_SLOTS; ++i) {
        if (gl->retiredTextures[i] == 0) continue;
        glDeleteTextures(1, &gl->retiredTextures[i]);
        gl->retiredTextures[i] = 0;
        gl->retiredTextureFrames[i] = 0;
        gl->retiredTextureBytes[i] = 0;
    }
    vglForceGarbageCollection();
}

void GLLegacyRenderer_prepareRoomTextureSet(GLLegacyRenderer* gl, Room* room) {
    if (gl == nullptr || room == nullptr || !room->payloadLoaded || gl->textureRoomRequired == nullptr) return;
    extern int g_vitaActiveChapter;
#ifdef PLATFORM_VITA
    // Phase 10: per-room texture telemetry, for recalibrating cache limits and
    // pools with real data. R444/BC3/PVRTC counts are inferred from the effective
    // bytes-per-pixel of each resident page (RGBA4444 ~2.0, BC3 ~1.0, PVRTC2 ~0.5).
    {
        extern int g_vitaProbeLoggingEnabled;
        extern int g_vitaTextureFormatProfile;
        if (g_vitaProbeLoggingEnabled) {
            uint32_t residentPages = 0, r444 = 0, bc3 = 0, pvrtc = 0, pinnedResident = 0;
            for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
                if (!gl->textureLoaded[i]) continue;
                residentPages++;
                if (gl->texturePinned[i]) pinnedResident++;
                uint64_t bytes = gl->textureGpuBytes ? gl->textureGpuBytes[i] : 0;
                uint64_t px = (uint64_t)gl->textureWidths[i] * (uint64_t)gl->textureHeights[i];
                if (px == 0) continue;
                uint64_t bpp10 = bytes * 10ULL / px; // tenths of a byte per pixel
                if (bpp10 >= 15) r444++;
                else if (bpp10 >= 8) bc3++;
                else pvrtc++;
            }
            char line[224];
            int n = snprintf(line, sizeof(line),
                "ROOM_TXTR ch=%d room=%s mode=%d pages=%u resident=%u r444=%u bc3=%u pvrtc=%u pinned=%u residentMiB=%llu evict=%u defer=%u ramhit=%u\n",
                g_vitaActiveChapter, room->name ? room->name : "<null>",
                g_vitaTextureFormatProfile, gl->textureCount, residentPages,
                r444, bc3, pvrtc, pinnedResident,
                (unsigned long long)(gl->residentTextureBytes / (1024ULL * 1024ULL)),
                gl->vitaTextureEvictions, gl->vitaTextureDeferred, gl->vitaTextureRamHits);
            if (n > 0) {
                SceUID fd = sceIoOpen("ux0:data/deltarune/deltarunevita/butterscotch-probe.log",
                                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
                if (fd >= 0) {
                    sceIoWrite(fd, line, (SceSize)((size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1));
                    sceIoClose(fd);
                }
            }
        }
    }
#endif
    bool previousTeacupBc3Mode = vitaCh2TeacupBc3Mode;
    bool nextTeacupBc3Mode = g_vitaActiveChapter == 2 && room->name != nullptr &&
        strcmp(room->name, "room_dw_cyber_teacup_final") == 0;
    // Texture-page IDs are shared by many Chapter 2 rooms. A page uploaded as
    // BC3 must not remain cached when the destination expects its normal
    // RGBA4444/PVRTC2 representation, otherwise later rooms sample a texture
    // created under the previous room policy.
    if (previousTeacupBc3Mode && !nextTeacupBc3Mode) {
        static const uint32_t bc3Pages[] = {6U, 21U};
        for (uint32_t i = 0; i < sizeof(bc3Pages) / sizeof(bc3Pages[0]); ++i)
            vitaEvictSpecificTexturePage(gl, bc3Pages[i]);
    }
    vitaCh2TeacupBc3Mode = nextTeacupBc3Mode;
    if (g_vitaActiveChapter == 2 && room->name != nullptr) {
        vitaCh2TexturePolicyRegion =
            (strstr(room->name, "dw_cyber") || strstr(room->name, "dw_city")) ? 3 :
            strstr(room->name, "dw_mansion") ? 4 :
            (strstr(room->name, "dw_castle") || strstr(room->name, "dw_ralsei") ||
             strcmp(room->name, "room_shop1") == 0) ? 2 : 1;
    }
    vitaCh2CyberIntroMemoryMode = g_vitaActiveChapter == 2 &&
        room->name != nullptr &&
        (strcmp(room->name, "room_dw_cyber_intro_2") == 0 ||
         strcmp(room->name, "room_dw_cyber_intro_connector") == 0);
    vitaCh2KeyboardMemoryMode = g_vitaActiveChapter == 2 &&
        room->name != nullptr && strstr(room->name, "room_dw_cyber_keyboard") != nullptr;
    vitaCh2BattleMemoryMode = g_vitaActiveChapter == 2 && room->name != nullptr &&
        strstr(room->name, "battle") != nullptr;
    vitaCh2GigaSequenceMode = g_vitaActiveChapter == 2 && room->name != nullptr &&
        (strcmp(room->name, "room_dw_mansion_east_4f_d") == 0 ||
         strcmp(room->name, "room_dw_mansion_top") == 0 ||
         strcmp(room->name, "room_dw_mansion_gigaqueen") == 0);
    bool vitaCouchOverworld = room->name != nullptr &&
        strncmp(room->name, "room_dw_couch_overworld", sizeof("room_dw_couch_overworld") - 1) == 0;
    bool vitaChapter3TvBoard = room->name != nullptr && g_vitaActiveChapter == 3 &&
        (strncmp(room->name, "room_ch3_", sizeof("room_ch3_") - 1) == 0 ||
         strncmp(room->name, "room_dw_teevie_", sizeof("room_dw_teevie_") - 1) == 0 ||
         strncmp(room->name, "room_dw_tv_", sizeof("room_dw_tv_") - 1) == 0 ||
         strncmp(room->name, "room_board_", sizeof("room_board_") - 1) == 0);
    bool vitaChapter3BoardMinigame = room->name != nullptr &&
        g_vitaActiveChapter == 3 &&
        strncmp(room->name, "room_board_1", sizeof("room_board_1") - 1) == 0;
    bool vitaChapter3Rhythm = room->name != nullptr && g_vitaActiveChapter == 3 &&
        strncmp(room->name, "room_dw_rhythm", sizeof("room_dw_rhythm") - 1) == 0;
    bool vitaChapter3PerformanceSurface = room->name != nullptr && g_vitaActiveChapter == 3 &&
        (vitaChapter3Rhythm ||
         strncmp(room->name, "room_dw_teevie_large_", sizeof("room_dw_teevie_large_") - 1) == 0 ||
         strncmp(room->name, "room_dw_teevie_susiezilla", sizeof("room_dw_teevie_susiezilla") - 1) == 0);
    bool vitaChapter3Intro = room->name != nullptr && g_vitaActiveChapter == 3 &&
        strcmp(room->name, "room_intro") == 0;
    bool vitaChapter3GreenRoom = room->name != nullptr && g_vitaActiveChapter == 3 &&
        strcmp(room->name, "room_dw_green_room") == 0;
    bool vitaChapter3Preview = room->name != nullptr && g_vitaActiveChapter == 3 &&
        strcmp(room->name, "room_dw_teevie_preview") == 0;
    bool vitaChapter3Ranking = room->name != nullptr && g_vitaActiveChapter == 3 &&
        strcmp(room->name, "room_dw_ranking_hub") == 0;
    bool vitaChapter5Town = room->name != nullptr &&
        (strncmp(room->name, "room_town_", sizeof("room_town_") - 1) == 0 ||
         strncmp(room->name, "room_dw_castle_", sizeof("room_dw_castle_") - 1) == 0 ||
         strncmp(room->name, "room_dw_ralsei_castle_", sizeof("room_dw_ralsei_castle_") - 1) == 0);
    vitaChapter5CastleApproachMode = g_vitaActiveChapter == 5 && room->name != nullptr &&
        strcmp(room->name, "room_dw_castle_area_1") == 0;
    bool vitaChapter5FloweryBattle = room->name != nullptr &&
        g_vitaActiveChapter == 5 &&
        strcmp(room->name, "room_dw_fcastle_flowery") == 0;
    vitaFloweryBattleMemoryMode = vitaChapter5FloweryBattle;
    vitaChapter3TvMemoryMode = vitaChapter3TvBoard;
    vitaChapter3BoardMinigameMode = vitaChapter3BoardMinigame;
    vitaChapter3PerformanceSurfaceMode = vitaChapter3PerformanceSurface;
    vitaChapter3GreenRoomMode = vitaChapter3GreenRoom;
    if (vitaChapter3PerformanceSurface) {
        fprintf(stderr, "CH3_PERF_MODE room=%s rhythm_preload=%d rgba4_surfaces=1\n",
                room->name, vitaChapter3Rhythm ? 1 : 0);
    }
    vitaCouchOverworldMemoryMode = g_vitaActiveChapter == 3 && vitaCouchOverworld;
    vitaNativeVideoMemoryMode = g_vitaActiveChapter == 3 && room->name != nullptr &&
        strcmp(room->name, "room_dw_couch_video") == 0;
    gl->vitaSevereRoomTextures = room->name != nullptr &&
        (strcmp(room->name, "room_dw_mansion_east_1f_c") == 0 ||
         strcmp(room->name, "room_dw_mansion_acid_tunnel_loop_rouxls") == 0 ||
         // The MP4 decoder needs contiguous user/GPU memory. Merely evicting
         // resident atlas textures left the two CPU staging pages alive, so
         // room creation entered VitaVideo_init with only ~3.6 MiB free and
         // crashed before video_open could emit its first log marker.
         vitaNativeVideoMemoryMode ||
         (g_vitaActiveChapter == 3 && vitaCouchOverworld) ||
         vitaChapter3TvBoard ||
         (g_vitaActiveChapter == 5 && (vitaChapter5Town || vitaChapter5FloweryBattle)));
    gl->vitaConstrainedRoomTextures = room->name != nullptr &&
        (strcmp(room->name, "room_dw_castle_area_2_transformed") == 0 ||
         strcmp(room->name, "room_dw_cyber_rollercoaster") == 0 ||
         gl->vitaSevereRoomTextures);
    // Preserve the proven v0.68-3 Chapter 2 behaviour: pages load lazily on
    // first draw and remain available across adjoining castle/city rooms.
    // Camera-constrained residency was the regression that made characters
    // and backgrounds disappear in castle_area_2_transformed/rollercoaster.
    if (g_vitaActiveChapter == 2) {
        gl->vitaSevereRoomTextures = false;
        gl->vitaConstrainedRoomTextures = room->name != nullptr &&
            (strcmp(room->name, "room_dw_castle_area_2_transformed") == 0 ||
             strcmp(room->name, "room_shop1") == 0 ||
             strcmp(room->name, "room_dw_cyber_viro_ring") == 0 ||
             strcmp(room->name, "room_dw_cyber_teacup_final") == 0 ||
             strcmp(room->name, "room_dw_cyber_rollercoaster") == 0 ||
             strcmp(room->name, "room_dw_cyber_maze_queenscreen") == 0 ||
             strcmp(room->name, "room_dw_cyber_musical_shop") == 0);
        if (vitaCh2CyberIntroMemoryMode)
            gl->vitaConstrainedRoomTextures = true;
    }
    if (gl->vitaSevereRoomTextures) {
        // Do not carry the previous room's full-resolution CPU staging page
        // into a room already proven to exhaust the auxiliary RAM pool.
        for (int i = 0; i < VITA_CPU_TEXTURE_CACHE_SLOTS; ++i) {
            free(gl->cpuTextureCachePixels[i]);
            gl->cpuTextureCachePixels[i] = nullptr;
            gl->cpuTextureCachePage[i] = UINT32_MAX;
            gl->cpuTextureCacheStamp[i] = 0;
            gl->cpuTextureCacheWidth[i] = 0;
            gl->cpuTextureCacheHeight[i] = 0;
        }
    }
    memset(gl->textureRoomRequired, 0, gl->originalTexturePageCount * sizeof(*gl->textureRoomRequired));
    if (gl->textureRoomPreload != nullptr)
        memset(gl->textureRoomPreload, 0, gl->originalTexturePageCount * sizeof(*gl->textureRoomPreload));
    if (gl->textureRetryAfterFrame != nullptr)
        memset(gl->textureRetryAfterFrame, 0,
               gl->originalTexturePageCount * sizeof(*gl->textureRetryAfterFrame));
    if (gl->textureRetryDelayFrames != nullptr)
        memset(gl->textureRetryDelayFrames, 0,
               gl->originalTexturePageCount * sizeof(*gl->textureRetryDelayFrames));
    gl->textureRoomChangeFrame = gl->textureFrame;

    for (uint32_t i = 0; i < 8; ++i)
        if (room->backgrounds != nullptr && room->backgrounds[i].enabled)
            vitaMarkBackgroundPage(gl, room->backgrounds[i].backgroundDefinition);
    for (uint32_t i = 0; i < room->tileCount; ++i) {
        RoomTile* tile = &room->tiles[i];
        if (tile->useSpriteDefinition) vitaMarkSpritePages(gl, tile->backgroundDefinition);
        else vitaMarkBackgroundPage(gl, tile->backgroundDefinition);
    }

    for (uint32_t i = 0; i < room->layerCount; ++i) {
        RoomLayer* layer = &room->layers[i];
        if (!layer->visible) continue;
        if (layer->type == RoomLayerType_Background && layer->backgroundData != nullptr) {
            if (layer->backgroundData->visible)
                vitaMarkSpritePages(gl, layer->backgroundData->spriteIndex);
        } else if (layer->type == RoomLayerType_Tiles && layer->tilesData != nullptr) {
            vitaMarkBackgroundPage(gl, layer->tilesData->backgroundIndex);
        } else if (layer->type == RoomLayerType_Assets && layer->assetsData != nullptr) {
            RoomLayerAssetsData* assets = layer->assetsData;
            for (uint32_t s = 0; s < assets->spriteCount; ++s)
                vitaMarkSpritePages(gl, assets->sprites[s].spriteIndex);
            for (uint32_t t = 0; t < assets->legacyTileCount; ++t) {
                RoomTile* tile = &assets->legacyTiles[t];
                if (tile->useSpriteDefinition) vitaMarkSpritePages(gl, tile->backgroundDefinition);
                else vitaMarkBackgroundPage(gl, tile->backgroundDefinition);
            }
        }
    }

    // Static backgrounds, tiles and asset-layer sprites are safe to prepare
    // while the loading screen is active. Default sprites from every object
    // are only a conservative residency manifest: many never appear in the
    // room's initial state and preloading them caused a 34-second Create plus
    // a completely full VitaGL pool in room_town_south.
    if (gl->textureRoomPreload != nullptr)
        memcpy(gl->textureRoomPreload, gl->textureRoomRequired,
               gl->originalTexturePageCount * sizeof(*gl->textureRoomPreload));

    // The TV curtain, cameras and Tenna are dynamic actors rather than static
    // room assets. Their four shared pages were consequently absent from the
    // destination preload manifest and alternated through the 76 MiB cache:
    // Tenna disappeared/corrupted, the curtain vanished and the cameras used
    // an incomplete page. Resolve the pages by sprite name (not numeric page,
    // which differs after a translation repack) and carry this compact core
    // through the Chapter 3 TV/gameshow/board sequence.
    // room_dw_rhythm creates most of its stage/UI resources from GML after
    // Room Start. The static room manifest is therefore nearly empty and the
    // first playable beats were uploading atlases on demand. Warm only the
    // semantic rhythm set during the transition; do not pin it.
    if (vitaChapter3Rhythm) {
        static const char* rhythmCore[] = {
            "spr_rhythmgame_bg", "spr_rhythmgame_bg_mask",
            "spr_rhythmgame_button", "spr_rhythmgame_performer_kris",
            "spr_rhythmgame_performer_ralsei", "spr_rhythmgame_performer_susie",
            "spr_rhythmgame_popularity", "spr_rhythmgame_spotlight",
            "spr_rhythmgame_stage_palette", "spr_rhythmgame_overlay",
            "spr_popularity_reduce", "spr_popularity_raise",
            "spr_rhythmgame_countdown", "spr_ptc_performance_bad",
            "spr_ptc_performance_good", "spr_text_timing_bad",
            "spr_text_timing_good", "spr_text_timing_perfect"
        };
        for (uint32_t i = 0; i < sizeof(rhythmCore) / sizeof(rhythmCore[0]); ++i)
            vitaMarkSpritePagesByName(gl, rhythmCore[i], true);
    }

    if (vitaChapter3TvBoard) {
        vitaMarkSpritePagesByName(gl, "spr_dw_teevie_curtain", true);
        vitaMarkSpritePagesByName(gl, "spr_placeholder1653", true);
        vitaMarkSpritePagesByName(gl, "spr_tenna_hooray", true);
        vitaMarkSpritePagesByName(gl, "spr_tenna_pose", true);
        // The gameshow battle introduces these only after Susie's attack and
        // the stage transition.  Loading their pages at that instant caused a
        // visible pause (and an accompanying audio read) even though the room
        // itself had already settled.  Promote the compact stage set during
        // the room transition; names survive RectPOT/language repacks.
        vitaMarkSpritePagesByName(gl, "spr_gameshow_stage_fire", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_gameshow_tv_bg_mask", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_gameshow_bg", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_gameshow_curtain", true);
        vitaMarkSpritePagesByName(gl, "spr_gameshow_screen_lights", true);
        vitaMarkSpritePagesByName(gl, "spr_gameshow_palette", true);
        if (vitaChapter3BoardMinigame) {
            // Battle/board sprites are selected by GML well after Room Start.
            // Promote one semantic resource from each late atlas observed in
            // the Vita trace. Names remain valid after RectPOT and translation
            // repacks, unlike the recorded page IDs.
            static const char* boardCore[] = {
                "spr_board_npc_tenna", "spr_board_lightningbullet_diag",
                "spr_krisb_act", "spr_board_enemy_shadowman_hit_hat",
                "spr_btdefend", "spr_btact", "spr_bnamethrash",
                "spr_bnameralsei", "spr_btitem",
                "spr_battleblcon_v2_left", "spr_shadowman_idle_hat",
                "spr_battlemsg", "spr_board_numbers",
                "spr_board_cactus_spines", "spr_board_smallbullet",
                "spr_board_spear", "spr_board_excl"
            };
            for (uint32_t i = 0; i < sizeof(boardCore) / sizeof(boardCore[0]); ++i)
                vitaMarkSpritePagesByName(gl, boardCore[i], true);
        }
    }

    // These early Chapter 3 scenes select most of their scenery and cutscene
    // frames from GML after Room Start. The static room manifest therefore
    // missed them and the captured session paid six 0.55-0.74 second uploads
    // while the intro was already visible, followed by 4-6 second first
    // frames in Green Room/Ranking. Resolve by semantic resource name so the
    // optimized and translated data.win layouts remain compatible.
    if (vitaChapter3Intro) {
        vitaMarkSpritePagesByName(gl, "spr_introimage0", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage1", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage2", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage3", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage4", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage5", true);
        vitaMarkSpritePagesByName(gl, "spr_introimage6", true);
            // obj_intro_tv_time is created after Room Start. Its logo frames are not
        // discoverable from static room instances, so promote them here as part
        // of the same intro manifest. This keeps their first upload behind the
        // room transition instead of stalling the visible TV Time animation.
        vitaMarkSpritePagesByName(gl, "spr_dw_tv_time_intro", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_tv_time_intro_loop", true);
}
    if (vitaChapter3GreenRoom) {
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_cover", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_bg_fx_floorgradient", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_bg_fx_wallrow", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_bg_fx_wallstars", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_bar_back", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_bar_front", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_couch_left", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_couch_right", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_door", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_green_room_tv", true);
    }
    if (vitaChapter3Preview) {
        vitaMarkSpritePagesByName(gl, "spr_dw_teevie_preview_curtain", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_tvland_preview_door", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_tvland_preview_vending", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_teevie_screen", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_teevie_screen_frame", true);
    }
    if (vitaChapter3Ranking) {
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_counter", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_doors", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_doors_locked", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_t_screen", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_z_mirror", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_ranking_z_phone", true);
    }

    // Chapter 2 PT-BR repacks texture pages, so protect dynamic Cyber assets
    // by resource name instead of hard-coded atlas IDs. These backgrounds and
    // floor-hole sprites are selected by GML only after Room Start and were
    // absent from the static room manifest at a full 104 MiB cache.
    if (g_vitaActiveChapter == 2 && room->name != nullptr) {
        bool cyberIntro = strcmp(room->name, "room_dw_cyber_intro_2") == 0 ||
                          strcmp(room->name, "room_dw_cyber_intro_connector") == 0;
        bool keyboardRoom = strstr(room->name, "room_dw_cyber_keyboard") != nullptr;
        if (cyberIntro) {
            vitaMarkSpritePagesByName(gl, "spr_cyber_hole_lights", true);
        }
        if (keyboardRoom) {
            // These resources are named like backgrounds but are SPRT entries
            // in the Steam/PT-BR data. Looking only in BGND silently marked
            // nothing and allowed the keyboard atlas to be replaced.
            vitaMarkSpritePagesByName(gl, "bg_dw_cyber_keyboard", true);
            vitaMarkSpritePagesByName(gl, "bg_dw_cyber_keyboard_big", true);
            vitaMarkSpritePagesByName(gl, "bg_dw_cyber_keyboard_bigger", true);
            vitaMarkSpritePagesByName(gl, "spr_ch2_keyboard_tile", true);
            vitaMarkSpritePagesByName(gl, "spr_ch2_keyboard_tile_city", true);
            vitaMarkSpritePagesByName(gl, "spr_ch2_keyboard_screen", true);
        }
        if (vitaCh2GigaSequenceMode) {
            // The Queen introduction and Giga Queen battle choose their city
            // and speed layers from GML after Room Start. They are SPRT entries
            // in newer data.win layouts, so static BGND discovery misses them.
            vitaMarkSpritePagesByName(gl, "bg_cityscape", true);
            vitaMarkSpritePagesByName(gl, "bg_speed", true);
            vitaMarkSpritePagesByName(gl, "bg_cover", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_laser_cityscape", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_laser_cityscape_mask", true);
            vitaMarkSpritePagesByName(gl, "spr_mansion_ferris_wheel_bg", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_queen_giga_head", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_queen_giga_hand", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_queen_giga_fist_left", true);
            vitaMarkSpritePagesByName(gl, "spr_cutscene_26_queen_giga_bonds", true);
        }
    }

    // Flowery switches these resources dynamically after Room Start, so they
    // are absent from the static room manifest. Resolve by sprite name: page
    // IDs differ between the original and translated layouts. This covers
    // the chase thorns, yellow-heart phase and Susie's Idea animation without
    // pinning unrelated atlases for the complete battle.
    if (vitaChapter5FloweryBattle && gl->textureRoomPreload != nullptr) {
        vitaMarkSpritePagesByName(gl, "spr_bramble", true);
        vitaMarkSpritePagesByName(gl, "spr_enemy_floradin_spikes", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_garden_susiechase_spikery", true);
        vitaMarkSpritePagesByName(gl, "spr_susie_walk_right_dw_flowery_frazzled", true);
        vitaMarkSpritePagesByName(gl, "spr_yellowheart", true);
        vitaMarkSpritePagesByName(gl, "spr_yellow_centered", true);
        // The petal burst is created only at the phase change. Loading its
        // atlas from Draw produced a measured 764 ms frame followed by an
        // eviction storm. Prepare its known sprite variants behind loading.
        vitaMarkSpritePagesByName(gl, "spr_spinning_petal", true);
        vitaMarkSpritePagesByName(gl, "spr_petalGold", true);
        vitaMarkSpritePagesByName(gl, "spr_dw_three_petals", true);
    }

    if (vitaChapter5CastleApproachMode && gl->textureRoomPreload != nullptr) {
        // The transformation assigns these actors dynamically. The static
        // manifest therefore missed them and the first visible frame paid a
        // measured 318 ms texture upload inside obj_mainchara.
        vitaMarkSpritePagesByName(gl, "spr_krisd", true);
        vitaMarkSpritePagesByName(gl, "spr_susied", true);
        vitaMarkSpritePagesByName(gl, "spr_ralseid", true);
    }

    DataWin* dw = gl->base.dataWin;
    for (uint32_t i = 0; i < room->gameObjectCount; ++i) {
        int32_t objectIndex = room->gameObjects[i].objectDefinition;
        if (objectIndex >= 0 && (uint32_t)objectIndex < dw->objt.count)
            vitaMarkSpritePages(gl, dw->objt.objects[objectIndex].spriteId);
    }
}

uint32_t GLLegacyRenderer_preloadRoomTextureSet(GLLegacyRenderer* gl) {
    if (gl == nullptr || gl->textureRoomRequired == nullptr) return 0;
    uint32_t loaded = 0;
#ifdef PLATFORM_VITA
    extern int g_vitaActiveChapter;
    // Chapter 2 must keep the v0.68-3 lazy-load behaviour. Preloading two
    // metadata pages consumed the transition reserve before Teacup's actual
    // visible pages 20/23/24 were requested, leaving them deferred forever.
    if (g_vitaActiveChapter == 2 && !vitaCh2GigaSequenceMode) return 0;
    // Chapter 5 rooms can advertise more than a dozen static 2048x2048
    // atlases. Loading all of them before construction filled the VitaGL,
    // RAM and PHY pools simultaneously (13 pages / ~63 MiB tracked in
    // room_town_south), turning the first frames into minute-long stalls.
    // Prepare a small visible core and let normal drawing bring in the rest
    // through the bounded, eviction-aware path.
    // Two pages are enough to establish the room. Loading four static pages
    // before town_south's first real draw displaced the pages that draw then
    // requested and contributed to the save-load VRAM spike.
    // Chapter 3's gameshow/board rooms advertise most of the shared TV atlas
    // set as static. Preloading all of it immediately refills the 96 MiB cache
    // after the transition trim and leaves pages 9/10/12/25/26/34/35 cycling.
    // Constrained rooms must not preload atlas pages selected only from room
    // metadata: Chapter 2 city commonly loaded two pages here, evicted them on
    // the first draw and then paid the same upload again in battle. Let the
    // camera select the first visible pages. Other heavy rooms keep a small
    // two-page bootstrap.
    // Flowery receives a dedicated 32 MiB transition trim, so its static
    // pages can be uploaded during the loading transition. Deferring both to
    // the first battle frame caused the black/missing background and a large
    // render hitch even when enough memory remained.
    const uint32_t preloadLimit = (gl->vitaConstrainedRoomTextures &&
                                   !vitaFloweryBattleMemoryMode) ? 0U :
        ((g_vitaActiveChapter == 5 ||
          (g_vitaActiveChapter == 3 && gl->vitaSevereRoomTextures)) ? 2U : UINT32_MAX);
#else
    const uint32_t preloadLimit = UINT32_MAX;
#endif
    // Load the small truly-global set (main font and playable Kris) immediately
    // after the transition trim, before room construction can fill the budget
    // with scenery. This removes the need to evict an atlas already referenced
    // by the current frame just to make the player or text visible.
    for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
        if (gl->texturePinned[i] && !gl->textureLoaded[i]) {
            if (GLLegacyRenderer_ensureTextureLoaded(gl, i)) loaded++;
        }
    }
    // The video itself replaces the room background. Keep only pinned
    // font/subtitle pages; static room pages would consume the contiguous
    // decoder reserve without contributing to the presented frame.
    if (vitaNativeVideoMemoryMode) return loaded;
    if (vitaCh2GigaSequenceMode) {
        // Move the small, repeatedly observed Queen/Giga Queen core behind the
        // room loading screen. Loading every cutscene_26/27 page would consume
        // the entire Vita cache; this named set fixes the missing background
        // and the largest phase-change stalls without pinning the full movie.
        loaded += vitaEnsureSpritePagesByName(gl, "bg_cityscape");
        loaded += vitaEnsureSpritePagesByName(gl, "bg_speed");
        loaded += vitaEnsureSpritePagesByName(gl, "bg_cover");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_laser_cityscape");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_laser_cityscape_mask");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_mansion_ferris_wheel_bg");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_queen_giga_head");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_queen_giga_hand");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_queen_giga_fist_left");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_cutscene_26_queen_giga_bonds");
        return loaded;
    }
    // The first slide dynamically switches five character/effect atlases that
    // are not part of the room's static manifest. Upload them while the room
    // loading transition is still visible instead of stalling the collision
    // event on first use. Resource names survive translated atlas repacks.
    if (vitaCouchOverworldMemoryMode) {
        loaded += vitaEnsureSpritePagesByName(gl, "spr_krisd_slide");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_krisd_slide_heart");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_krisd_slide_light");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_ralsei_slide");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_susie_slide");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_slidedust");
        return loaded;
    }
    // Constrained Chapter 3 rooms normally skip metadata preloading. Allow
    // only the explicitly promoted TV core above; loading it during the room
    // transition avoids first-draw atlas replacement and keeps the minigame
    // from entering while those same pages are still being recycled.
    if (vitaChapter3TvMemoryMode) {
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_teevie_curtain");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_placeholder1653");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_tenna_hooray");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_tenna_pose");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_gameshow_stage_fire");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_gameshow_tv_bg_mask");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_gameshow_bg");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_gameshow_curtain");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_gameshow_screen_lights");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_gameshow_palette");
        if (vitaChapter3BoardMinigameMode) {
            static const char* boardCore[] = {
                "spr_board_npc_tenna", "spr_board_lightningbullet_diag",
                "spr_krisb_act", "spr_board_enemy_shadowman_hit_hat",
                "spr_btdefend", "spr_btact", "spr_bnamethrash",
                "spr_bnameralsei", "spr_btitem",
                "spr_battleblcon_v2_left", "spr_shadowman_idle_hat",
                "spr_battlemsg", "spr_board_numbers",
                "spr_board_cactus_spines", "spr_board_smallbullet",
                "spr_board_spear", "spr_board_excl"
            };
            for (uint32_t i = 0; i < sizeof(boardCore) / sizeof(boardCore[0]); ++i)
                loaded += vitaEnsureSpritePagesByName(gl, boardCore[i]);
        }
        // Do not combine this core with the generic static preload. On the
        // first room transition Chapter 3 has just retired its bootstrap
        // pages; submitting two more atlas uploads before VitaGL has reclaimed
        // that storage caused a GPU crash immediately after texture trimming.
        return loaded;
    }
    if (vitaFloweryBattleMemoryMode) {
        loaded += vitaEnsureSpritePagesByName(gl, "spr_bramble");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_garden_susiechase_spikery");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_susie_walk_right_dw_flowery_frazzled");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_yellowheart");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_yellow_centered");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_spinning_petal");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_petalGold");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_dw_three_petals");
    }
    if (vitaChapter5CastleApproachMode) {
        loaded += vitaEnsureSpritePagesByName(gl, "spr_krisd");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_susied");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_ralseid");
    }
#ifdef PLATFORM_VITA
    if (g_vitaActiveChapter == 2 && gl->base.runner != nullptr &&
        gl->base.runner->currentRoom != nullptr &&
        strcmp(gl->base.runner->currentRoom->name,"room_dw_cyber_maze_queenscreen") == 0) {
        static const char* intro[] = {
            "spr_krisb_attackready", "spr_susieb_attackready", "spr_ralseib_attackready",
            "spr_susieb_idle_unarmed", "spr_susieb_idle", "spr_virovirokun_idle",
            "spr_virovirokun_hurt", "spr_virovirokun_spared"
        };
        for (uint32_t i=0;i<sizeof(intro)/sizeof(intro[0]);i++)
            loaded += vitaEnsureSpritePagesByName(gl,intro[i]);
        fprintf(stderr,"CH2_BATTLE_INTRO=room_prewarm loaded=%u\n",loaded);
    }
#endif
    if (vitaChapter3GreenRoomMode) {
        // Room trimming can discard the chapter-start battle warmup. Refill
        // its named core behind this room's loading transition, before the
        // first encounter freezes the weapon-draw animation. Do not pin it.
        loaded += GLLegacyRenderer_preloadChapterBattleCore(gl);
        loaded += vitaEnsureSpritePagesByName(gl, "spr_headsusie");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_headralsei");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_sparestar");
        loaded += vitaEnsureSpritePagesByName(gl, "spr_tiredmark");
        fprintf(stderr, "CH3_GREEN_BATTLE_CORE=room_prewarm loaded=%u\n", loaded);
    }
    if (gl->base.runner != nullptr && gl->base.runner->currentRoom != nullptr &&
        strcmp(gl->base.runner->currentRoom->name, "room_dw_snow_zone_battle") == 0) {
        // Chapter-start warmup has already been evicted by the time Tenna
        // starts. Refresh only the initial party/UI pages behind room loading;
        // never pin the entire boss or its later minigames in VRAM.
        static const char* const intro[] = {
            "spr_krisb_attackready", "spr_susieb_attackready", "spr_ralseib_attackready",
            "spr_krisb_idle", "spr_susieb_idle", "spr_ralseib_idle",
            "spr_susieb_idle_unarmed", "spr_battlemsg", "spr_battleblcon",
            "spr_tensionbar", "spr_btact", "spr_btfight"
        };
        uint32_t prepared = 0;
        for (uint32_t i = 0; i < sizeof(intro)/sizeof(intro[0]); i++)
            prepared += vitaEnsureSpritePagesByName(gl, intro[i]);
        loaded += prepared;
        fprintf(stderr, "CH3_TENNA_INTRO=room_prewarm loaded=%u\n", prepared);
    }
    uint32_t roomLoaded = 0;
    for (uint32_t i = 0; i < gl->originalTexturePageCount; ++i) {
        if (roomLoaded >= preloadLimit) break;
        if (gl->textureRoomPreload != nullptr && gl->textureRoomPreload[i] &&
            !gl->textureLoaded[i]) {
            if (GLLegacyRenderer_ensureTextureLoaded(gl, i)) {
                loaded++;
                roomLoaded++;
            }
        }
    }
    return loaded;
}
#endif

// Classifies a sprite quad against the actual GameMaker camera, rather than
// the 960x544 host framebuffer. DELTARUNE's 4:3 image occupies only the
// letterboxed game region; the console borders are rendered separately.
//
// 0 = far outside the camera (do not request its atlas)
// 1 = inside the read-ahead margin (upload at most one atlas this frame)
// 2 = visible (load and draw normally)
static int vitaClassifyCameraQuad(Renderer* renderer,
                                  float x0, float y0, float x1, float y1,
                                  float x2, float y2, float x3, float y3) {
#ifdef PLATFORM_VITA
    extern int g_vitaActiveChapter;
    // Match v0.68-3 for Chapter 2 by default. Some cutscenes transform or
    // reposition sprites before the camera catches up, so global culling hid
    // valid characters. The two measured Cyber City rooms below are safe:
    // their dominant cost is a large set of ordinary, off-camera background
    // and traffic quads. Restrict culling to those rooms instead of changing
    // the rendering behaviour of the whole chapter.
    if (g_vitaActiveChapter == 2) {
        const char* roomName = (renderer != nullptr && renderer->runner != nullptr &&
                                renderer->runner->currentRoom != nullptr)
            ? renderer->runner->currentRoom->name : nullptr;
        bool safeCityCull = roomName != nullptr &&
            (strcmp(roomName, "room_dw_city_traffic_3_2Entrances") == 0 ||
             strcmp(roomName, "room_dw_cyber_post_music_boss_slide") == 0 ||
             strcmp(roomName, "room_dw_cyber_viro_ring") == 0 ||
             strcmp(roomName, "room_dw_cyber_rollercoaster") == 0 ||
             strcmp(roomName, "room_dw_cyber_music_bullet") == 0 ||
             strcmp(roomName, "room_dw_cyber_maze_queenscreen") == 0 ||
             strcmp(roomName, "room_dw_cyber_musical_shop") == 0);
        if (!safeCityCull) return 2;
    }
    if (renderer == nullptr || renderer->runner == nullptr) return 2;
    if (renderer->cameraCurrent == GUI_CAMERA || renderer->cameraCurrent == SURFACE_CAMERA)
        return 2;

    GMLCamera* camera = Runner_getCameraById(renderer->runner, renderer->cameraCurrent);
    if (camera == nullptr || !camera->allocated || camera->viewWidth <= 0 ||
        camera->viewHeight <= 0 || fabsf(camera->viewAngle) > 0.01f)
        return 2;

    // Custom vertex shaders may move otherwise off-screen geometry. Their
    // output cannot be inferred from camera scalars or a CPU-only matrix.
    if (renderer->currentShader >= 0) return 2;
    const float xy[8]={x0,y0,x1,y1,x2,y2,x3,y3};
    float portW=renderer->CPortW>0 ? renderer->CPortW : 640;
    float portH=renderer->CPortH>0 ? renderer->CPortH : 480;
    return CameraQuad_classify(&renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION],
                               xy,256.0f/portW,256.0f/portH);
#else
    (void)renderer; (void)x0; (void)y0; (void)x1; (void)y1;
    (void)x2; (void)y2; (void)x3; (void)y3;
    return 2;
#endif
}

static bool vitaPrepareCameraTexture(GLLegacyRenderer* gl, uint32_t pageId,
                                     int cameraClass) {
#ifdef PLATFORM_VITA
    bool firstClassificationThisFrame = true;
    if (gl->textureCameraClass != nullptr && gl->textureCameraClassFrame != nullptr &&
        pageId < gl->originalTexturePageCount) {
        firstClassificationThisFrame = gl->textureCameraClassFrame[pageId] != gl->textureFrame;
        if (firstClassificationThisFrame) {
            gl->textureCameraClass[pageId] = (uint8_t)cameraClass;
            gl->textureCameraClassFrame[pageId] = gl->textureFrame;
        } else if ((uint8_t)cameraClass > gl->textureCameraClass[pageId]) {
            gl->textureCameraClass[pageId] = (uint8_t)cameraClass;
        }
    }
    if (cameraClass == 0) return false;
    if (cameraClass == 1 && !gl->textureLoaded[pageId]) {
        Texture* txtr = &gl->base.dataWin->txtr.textures[pageId];
        uint64_t expectedBytes = vitaGpuTextureBytes(gl, pageId,
                                                     txtr->textureWidth,
                                                     txtr->textureHeight);
        // Preserve one complete 2048x2048 RGBA4444 slot for a page that is
        // actually visible. Without this reserve, read-ahead filled the 68 MiB
        // Chapter 5 cache and visible Dark World backgrounds entered the
        // progressive retry loop.
        const uint64_t visibleReserve = 8ULL * 1024ULL * 1024ULL;
        uint64_t limit = vitaTextureCacheLimit();
        if (gl->residentTextureBytes + expectedBytes + visibleReserve > limit)
            return false;
        if (gl->vitaNearTextureLoadFrame == gl->textureFrame) return false;
        gl->vitaNearTextureLoadFrame = gl->textureFrame;
    }
#else
    (void)cameraClass;
#endif
    return GLLegacyRenderer_ensureTextureLoaded(gl, pageId);
}

static void glDrawSprite(Renderer* renderer, int32_t tpagIndex, float x, float y, float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->targetWidth;
    float localY1 = localY0 + (float) tpag->targetHeight;

    // Build 2D transform: T(x,y) * R(-angleDeg) * S(xscale, yscale)
    // GML rotation is counter-clockwise, OpenGL rotation is counter-clockwise, but
    // since we have Y-down, we negate the angle to get the correct visual rotation
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    // Transform 4 corners
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, localX0, localY0, &x0, &y0); // top-left
    Matrix4f_transformPoint(&transform, localX1, localY0, &x1, &y1); // top-right
    Matrix4f_transformPoint(&transform, localX1, localY1, &x2, &y2); // bottom-right
    Matrix4f_transformPoint(&transform, localX0, localY1, &x3, &y3); // bottom-left

    int cameraClass = vitaClassifyCameraQuad(renderer, x0, y0, x1, y1, x2, y2, x3, y3);
    if (!vitaPrepareCameraTexture(gl, (uint32_t)pageId, cameraClass)) return;
    // A near-camera request is read-ahead only. It becomes drawable as soon as
    // the quad crosses into the real 4:3 camera on a later frame.
    if (cameraClass != 2) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

#ifdef PLATFORM_VITA
    const Vita2DVertex vertices[4] = {
        {u0, v0, r, g, b, alpha, x0, y0},
        {u1, v0, r, g, b, alpha, x1, y1},
        {u1, v1, r, g, b, alpha, x2, y2},
        {u0, v1, r, g, b, alpha, x3, y3},
    };
    vitaDrawQuad(vertices);
#else
    glBegin(GL_QUADS);
        // Vertex 0: top-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x1, y1);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x2, y2);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x3, y3);
    glEnd();
#endif
    PS3_PALETTED_END();
}

static void glDrawSpriteTiled(Renderer* renderer, int32_t tpagIndex, float originX, float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];

    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float tileW = (float) tpag->boundingWidth * axScale;
    float tileH = (float) tpag->boundingHeight * ayScale;
    if (0 >= tileW || 0 >= tileH) return;

    float startX, endX, startY, endY;
    if (tileX) {
        startX = fmodf(x - originX * axScale, tileW);
        if (startX > 0) startX -= tileW;
        endX = roomW;
    } else {
        startX = x - originX * axScale;
        endX = startX + tileW;
    }
    if (tileY) {
        startY = fmodf(y - originY * ayScale, tileH);
        if (startY > 0) startY -= tileH;
        endY = roomH;
    } else {
        startY = y - originY * ayScale;
        endY = startY + tileH;
    }

#ifdef PLATFORM_VITA
    // GameMaker can request a tiled sprite across the complete room even
    // though only the active 4:3 camera is visible. Keep one surrounding tile
    // for seamless scrolling and avoid submitting the off-camera grid.
    if (renderer->cameraCurrent != GUI_CAMERA &&
        renderer->cameraCurrent != SURFACE_CAMERA) {
        GMLCamera* camera = Runner_getCameraById(renderer->runner,
                                                 renderer->cameraCurrent);
        if (camera != nullptr && camera->allocated && camera->viewWidth > 0 &&
            camera->viewHeight > 0 && fabsf(camera->viewAngle) <= 0.01f) {
            float clipL = camera->viewX - tileW;
            float clipT = camera->viewY - tileH;
            float clipR = camera->viewX + (float)camera->viewWidth + tileW;
            float clipB = camera->viewY + (float)camera->viewHeight + tileH;
            if (tileX && startX < clipL)
                startX += floorf((clipL - startX) / tileW) * tileW;
            if (tileY && startY < clipT)
                startY += floorf((clipT - startY) / tileH) * tileH;
            if (tileX && endX > clipR) endX = clipR;
            if (tileY && endY > clipB) endY = clipB;
        }
    }
#endif

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    // Use targetWidth/Height (draw size in bounding rect), not sourceWidth/Height (texture sample size).
    // They differ when the texture was auto-downscaled by GMS to fit a texture page.
    float localX0 = (float) tpag->targetX - originX;
    float localY0 = (float) tpag->targetY - originY;
    float localX1 = localX0 + (float) tpag->targetWidth;
    float localY1 = localY0 + (float) tpag->targetHeight;
    float sx0 = xscale * localX0;
    float sy0 = yscale * localY0;
    float sx1 = xscale * localX1;
    float sy1 = yscale * localY1;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Emit the entire tile grid in a single glBegin -> glEnd
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
    glColor4f(r, g, b, alpha);
    for (float dy = startY; endY > dy; dy += tileH) {
        float cy = dy + originY * ayScale;
        float vy0 = cy + sy0;
        float vy1 = cy + sy1;
        for (float dx = startX; endX > dx; dx += tileW) {
            float cx = dx + originX * axScale;
            float vx0 = cx + sx0;
            float vx1 = cx + sx1;

            glTexCoord2f(u0, v0); glVertex2f(vx0, vy0);
            glTexCoord2f(u1, v0); glVertex2f(vx1, vy0);
            glTexCoord2f(u1, v1); glVertex2f(vx1, vy1);
            glTexCoord2f(u0, v1); glVertex2f(vx0, vy1);
        }
    }
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePos(Renderer* renderer, int32_t tpagIndex, float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    int cameraClass = vitaClassifyCameraQuad(renderer, x1, y1, x2, y2, x3, y3, x4, y4);
    if (!vitaPrepareCameraTexture(gl, (uint32_t)pageId, cameraClass)) return;
    if (cameraClass != 2) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];
    glBindTexture(GL_TEXTURE_2D, texId);
    PS3_PALETTED_BEGIN(tpagIndex);

    float u0 = (float) tpag->sourceX / (float) texW;
    float v0 = (float) tpag->sourceY / (float) texH;
    float u1 = (float) (tpag->sourceX + tpag->sourceWidth) / (float) texW;
    float v1 = (float) (tpag->sourceY + tpag->sourceHeight) / (float) texH;

    glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v0);
        glVertex2f(x1, y1);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v0);
        glVertex2f(x2, y2);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u1, v1);
        glVertex2f(x3, y3);

        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glTexCoord2f(u0, v1);
        glVertex2f(x4, y4);
    glEnd();
    PS3_PALETTED_END();
}

static void glDrawSpritePart(Renderer* renderer, int32_t tpagIndex, int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return;

    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return;
    // Convert BGR color to RGB floats
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Quad corners (no origin offset - draw_sprite_part ignores sprite origin)
    float cx0, cy0, cx1, cy1, cx2, cy2, cx3, cy3;
    if (angleDeg == 0.0f) {
        cx0 = x;                         cy0 = y;
        cx1 = x + (float) srcW * xscale; cy1 = y;
        cx2 = x + (float) srcW * xscale; cy2 = y + (float) srcH * yscale;
        cx3 = x;                         cy3 = y + (float) srcH * yscale;
    } else {
        float angleRad = -angleDeg * ((float) M_PI / 180.0f);
        float cosA = cosf(angleRad);
        float sinA = sinf(angleRad);
        float qx0 = x,                         qy0 = y;
        float qx1 = x + (float) srcW * xscale, qy1 = y;
        float qx2 = x + (float) srcW * xscale, qy2 = y + (float) srcH * yscale;
        float qx3 = x,                         qy3 = y + (float) srcH * yscale;
        float dx, dy;
        dx = qx0 - pivotX; dy = qy0 - pivotY; cx0 = cosA * dx - sinA * dy + pivotX; cy0 = sinA * dx + cosA * dy + pivotY;
        dx = qx1 - pivotX; dy = qy1 - pivotY; cx1 = cosA * dx - sinA * dy + pivotX; cy1 = sinA * dx + cosA * dy + pivotY;
        dx = qx2 - pivotX; dy = qy2 - pivotY; cx2 = cosA * dx - sinA * dy + pivotX; cy2 = sinA * dx + cosA * dy + pivotY;
        dx = qx3 - pivotX; dy = qy3 - pivotY; cx3 = cosA * dx - sinA * dy + pivotX; cy3 = sinA * dx + cosA * dy + pivotY;
    }

    int cameraClass = vitaClassifyCameraQuad(renderer, cx0, cy0, cx1, cy1,
                                             cx2, cy2, cx3, cy3);
    if (!vitaPrepareCameraTexture(gl, (uint32_t)pageId, cameraClass)) return;
    if (cameraClass != 2) return;

    GLuint texId = gl->glTextures[pageId];
    int32_t texW = gl->textureWidths[pageId];
    int32_t texH = gl->textureHeights[pageId];
    glBindTexture(GL_TEXTURE_2D, texId);

    float u0 = (float) (tpag->sourceX + srcOffX) / (float) texW;
    float v0 = (float) (tpag->sourceY + srcOffY) / (float) texH;
    float u1 = (float) (tpag->sourceX + srcOffX + srcW) / (float) texW;
    float v1 = (float) (tpag->sourceY + srcOffY + srcH) / (float) texH;

    PS3_PALETTED_BEGIN(tpagIndex);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v0); glVertex2f(cx0, cy0);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v0); glVertex2f(cx1, cy1);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u1, v1); glVertex2f(cx2, cy2);

        glColor4f(r, g, b, alpha);
        glTexCoord2f(u0, v1); glVertex2f(cx3, cy3);
    glEnd();
    PS3_PALETTED_END();
}

// Emits a single colored quad into the batch using the white pixel texture
static void emitColoredQuad(GLLegacyRenderer* gl, float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

#ifdef PLATFORM_VITA
    const Vita2DVertex vertices[4] = {
        {0.5f, 0.5f, r, g, b, a, x0, y0},
        {0.5f, 0.5f, r, g, b, a, x1, y0},
        {0.5f, 0.5f, r, g, b, a, x1, y1},
        {0.5f, 0.5f, r, g, b, a, x0, y1},
    };
    vitaDrawQuad(vertices);
#else
    glBegin(GL_QUADS);
        // All UVs point to (0.5, 0.5) center of the 1x1 white texture
        // Vertex 0: top-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y0);

        // Vertex 1: top-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y0);

        // Vertex 2: bottom-right
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1, y1);

        // Vertex 3: bottom-left
        glColor4f(r, g, b, a);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x0, y1);
    glEnd();
#endif
}

static void glDrawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha, bool outline) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        emitColoredQuad(gl, x1, y1, x2 + 1, y1 + 1, r, g, b, alpha); // top
        emitColoredQuad(gl, x1, y2, x2 + 1, y2 + 1, r, g, b, alpha); // bottom
        emitColoredQuad(gl, x1, y1 + 1, x1 + 1, y2, r, g, b, alpha); // left
        emitColoredQuad(gl, x2, y1 + 1, x2 + 1, y2, r, g, b, alpha); // right
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects
        emitColoredQuad(gl, x1, y1, x2 + 1, y2 + 1, r, g, b, alpha);
    }
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha);
static void glDrawRectangleColor(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color1, uint32_t color2, uint32_t color3, uint32_t color4, float alpha, bool outline) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    float r3 = (float) BGR_R(color3) / 255.0f;
    float g3 = (float) BGR_G(color3) / 255.0f;
    float b3 = (float) BGR_B(color3) / 255.0f;

    float r4 = (float) BGR_R(color4) / 255.0f;
    float g4 = (float) BGR_G(color4) / 255.0f;
    float b4 = (float) BGR_B(color4) / 255.0f;

    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    if (outline) {
        // Draw 4 one-pixel-wide edges: top, bottom, left, right
        glDrawLineColor(renderer, x1, y1, x2, y1, 1.0, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y1, x2, y2, 1.0, color2, color3, alpha);
        glDrawLineColor(renderer, x2, y2, x1, y2, 1.0, color3, color4, alpha);
        glDrawLineColor(renderer, x1, y2, x1, y1, 1.0, color4, color1, alpha);
    } else {
        // Filled rectangle: GML adds +1 to width/height for filled rects

        // All UVs point to (0.5, 0.5) center of the 1x1 white texture
        glBegin(GL_QUADS);
            // Vertex 0: top-left
            glColor4f(r1, g1, b1, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1, y1); 

            // Vertex 1: top-right
            glColor4f(r2, g2, b2, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2+1, y1);

            // Vertex 2: bottom-right
            glColor4f(r3, g3, b3, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2+1, y2+1);

            // Vertex 3: bottom-left
            glColor4f(r4, g4, b4, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1, y2+1); 

        glEnd();
    }
}

// ===[ Line Drawing ]===

static void glDrawLine(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    // Vertex 0: start + perpendicular
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py);

        // Vertex 1: start - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py);

        // Vertex 2: end - perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py);

        // Vertex 3: end + perpendicular
        glColor4f(r, g, b, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py);
    glEnd();
}

static void glDrawLineColor(Renderer* renderer, float x1, float y1, float x2, float y2, float width, uint32_t color1, uint32_t color2, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    float r1 = (float) BGR_R(color1) / 255.0f;
    float g1 = (float) BGR_G(color1) / 255.0f;
    float b1 = (float) BGR_B(color1) / 255.0f;

    float r2 = (float) BGR_R(color2) / 255.0f;
    float g2 = (float) BGR_G(color2) / 255.0f;
    float b2 = (float) BGR_B(color2) / 255.0f;

    // Compute perpendicular offset for line thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (0.0001f > len) return;

    float halfW = width * 0.5f;
    float px = (-dy / len) * halfW;
    float py = (dx / len) * halfW;

    // Emit quad with per-vertex colors (color1 at start, color2 at end)
    glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

    glBegin(GL_QUADS);
        // Vertex 0: start + perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 + px, y1 + py); 

        // Vertex 1: start - perpendicular (color1)
        glColor4f(r1, g1, b1, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x1 - px, y1 - py); 

        // Vertex 2: end - perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 - px, y2 - py); 

        // Vertex 3: end + perpendicular (color2)
        glColor4f(r2, g2, b2, alpha);
        glTexCoord2f(0.5f, 0.5f);
        glVertex2f(x2 + px, y2 + py); 
    glEnd();
}

static void glDrawTriangle(Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha, bool outline)
{
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if(outline)
    {
        glDrawLineColor(renderer, x1, y1, x2, y2, 1, color1, color2, alpha);
        glDrawLineColor(renderer, x2, y2, x3, y3, 1, color2, color3, alpha);
        glDrawLineColor(renderer, x3, y3, x1, y1, 1, color3, color1, alpha);
    } else {
        glBindTexture(GL_TEXTURE_2D, gl->whiteTexture);

        glBegin(GL_TRIANGLES);
            glColor4f((float) BGR_R(color1) / 255.0f, (float) BGR_G(color1) / 255.0f, (float) BGR_B(color1) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x1 , y1);

            glColor4f((float) BGR_R(color2) / 255.0f, (float) BGR_G(color2) / 255.0f, (float) BGR_B(color2) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x2, y2);

            glColor4f((float) BGR_R(color3) / 255.0f, (float) BGR_G(color3) / 255.0f, (float) BGR_B(color3) / 255.0f, alpha);
            glTexCoord2f(0.5f, 0.5f);
            glVertex2f(x3, y3);
        glEnd();
    }
}

// ===[ Text Drawing ]===

// Resolved font state shared between glDrawText and glDrawTextColor
typedef struct {
    Font* font;
    TexturePageItem* fontTpag; // single TPAG for regular fonts (nullptr for sprite fonts)
    int32_t fontTpagIndex;     // TPAG index for regular fonts (-1 for sprite fonts)
    GLuint texId;
    int32_t texW, texH;
    Sprite* spriteFontSprite; // source sprite for sprite fonts (nullptr for regular fonts)
} GlFontState;

// Resolves font texture state
// Returns false if the font can't be drawn
static bool glResolveFontState(GLLegacyRenderer* gl, DataWin* dw, Font* font, GlFontState* state) {
    state->font = font;
    state->fontTpag = nullptr;
    state->fontTpagIndex = -1;
    state->texId = 0;
    state->texW = 0;
    state->texH = 0;
    state->spriteFontSprite = nullptr;

    if (!font->isSpriteFont) {
        int32_t fontTpagIndex = font->tpagIndex;
        if (0 > fontTpagIndex) return false;

        state->fontTpagIndex = fontTpagIndex;
        state->fontTpag = &dw->tpag.items[fontTpagIndex];
        int16_t pageId = state->fontTpag->texturePageId;
        if (0 > pageId || (uint32_t) pageId >= gl->textureCount) return false;
        if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;

        state->texId = gl->glTextures[pageId];
        state->texW = gl->textureWidths[pageId];
        state->texH = gl->textureHeights[pageId];
    } else if (font->spriteIndex >= 0 && dw->sprt.count > (uint32_t) font->spriteIndex) {
        state->spriteFontSprite = &dw->sprt.sprites[font->spriteIndex];
    }
    return true;
}

// Resolves UV coordinates, texture ID, and local position for a single glyph
// Returns false if the glyph can't be drawn
static bool glResolveGlyph(GLLegacyRenderer* gl, DataWin* dw, GlFontState* state, FontGlyph* glyph, float cursorX, float cursorY, GLuint* outTexId, int32_t* outTpagIdx, float* outU0, float* outV0, float* outU1, float* outV1, float* outLocalX0, float* outLocalY0) {
    Font* font = state->font;
    if (font->isSpriteFont && state->spriteFontSprite != nullptr) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t) (glyph - font->glyphs);
        if (0 > glyphIndex ||  glyphIndex >= (int32_t) sprite->textureCount) return false;

        int32_t tpagIdx = sprite->tpagIndices[glyphIndex];
        if (0 > tpagIdx) return false;

        TexturePageItem* glyphTpag = &dw->tpag.items[tpagIdx];
        int16_t pid = glyphTpag->texturePageId;
        if (0 > pid || (uint32_t) pid >= gl->textureCount) return false;
        if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pid)) return false;

        *outTexId = gl->glTextures[pid];
        *outTpagIdx = tpagIdx;
        int32_t tw = gl->textureWidths[pid];
        int32_t th = gl->textureHeights[pid];

        *outU0 = TextUtils_glyphUvStart(glyphTpag->sourceX, tw);
        *outV0 = TextUtils_glyphUvStart(glyphTpag->sourceY, th);
        *outU1 = TextUtils_glyphUvEnd(glyphTpag->sourceX, glyphTpag->sourceWidth, tw);
        *outV1 = TextUtils_glyphUvEnd(glyphTpag->sourceY, glyphTpag->sourceHeight, th);

        // Sprite-font glyphs sit at the cell offset. GM 2023.2+ subtracts the sprite origin, pre-2023.2 it cancels.
        // (See GameMaker-HTML5's commit a7c5b909209d5a28602fedfe2031965386a99921)
        *outLocalX0 = cursorX + (float) glyph->offset;
        *outLocalY0 = cursorY + (float) (int32_t) glyphTpag->targetY - (float) font->spriteOriginYAdjust;
    } else {
        *outTexId = state->texId;
        *outTpagIdx = state->fontTpagIndex;
        const int32_t glyphX = state->fontTpag->sourceX + glyph->sourceX;
        const int32_t glyphY = state->fontTpag->sourceY + glyph->sourceY;
        *outU0 = TextUtils_glyphUvStart(glyphX, state->texW);
        *outV0 = TextUtils_glyphUvStart(glyphY, state->texH);
        *outU1 = TextUtils_glyphUvEnd(glyphX, glyph->sourceWidth, state->texW);
        *outV1 = TextUtils_glyphUvEnd(glyphY, glyph->sourceHeight, state->texH);

        *outLocalX0 = cursorX + glyph->offset;
        *outLocalY0 = cursorY;
    }
    return true;
}

static void glDrawText(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, float lineSeparation) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    uint32_t color = renderer->drawColor;
    float alpha = renderer->drawAlpha;
    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    int32_t textLen = (int32_t) strlen(text);

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    // Per-line vertical stride. HTML5 runner's default `linesep` is `max_glyph_height * scaleY`.
    // We apply scaleY via the transform matrix below, so keep the stride in pre-scale (local) coords.
    // Caller-supplied separation is in world pre-scale pixels; divide by font->scaleY so the transform restores it.
    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from the per-line y offset
    // (see yyFont.GR_Text_Draw), shifting glyphs up so the baseline aligns with the drawn y.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4f(r, g, b, alpha);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();

                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    cursorX += TextUtils_getKerningOffset(glyph, nextCh);
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

static void glDrawTextColor(Renderer* renderer, const char* text, float x, float y, float xscale, float yscale, float angleDeg, int32_t _c1, int32_t _c2, int32_t _c3, int32_t _c4, float alpha, float lineSeparation) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    int32_t fontIndex = renderer->drawFont;
    if (0 > fontIndex || dw->font.count <= (uint32_t) fontIndex) return;

    Font* font = &dw->font.fonts[fontIndex];

    GlFontState fontState;
    if (!glResolveFontState(gl, dw, font, &fontState)) return;

    int32_t textLen = (int32_t) strlen(text);
    if(textLen == 0) return;

    // Count lines, treating \r\n and \n\r as single breaks
    int32_t lineCount = TextUtils_countLines(text, textLen);

    float lineStride = (0.0f > lineSeparation) ? TextUtils_lineStride(font) : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));

    // Vertical alignment offset
    float totalHeight = (float) lineCount * lineStride;
    float valignOffset = 0;
    if (renderer->drawValign == 1) valignOffset = -totalHeight / 2.0f;
    else if (renderer->drawValign == 2) valignOffset = -totalHeight;

    // Build transform matrix
    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale * font->scaleX, yscale * font->scaleY, angleRad);

    // Iterate through lines. HTML5 subtracts ascenderOffset from per-line y offset.
    float cursorY = valignOffset - (float) font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIdx = 0; lineCount > lineIdx; lineIdx++) {
        // Find end of current line
        int32_t lineEnd = lineStart;
        while (textLen > lineEnd && !TextUtils_isNewlineChar(text[lineEnd])) {
            lineEnd++;
        }
        int32_t lineLen = lineEnd - lineStart;

        // Horizontal alignment offset for this line
        float lineWidth = TextUtils_measureLineWidth(font, text + lineStart, lineLen);
        float halignOffset = 0;
        if (renderer->drawHalign == 1) halignOffset = -lineWidth / 2.0f;
        else if (renderer->drawHalign == 2) halignOffset = -lineWidth;

        float cursorX = halignOffset;
        // Pixel-position cursor for the gradient
        float gradientX = 0.0f;

        // Render each glyph in the line - decode each codepoint once and carry it forward as next iteration's ch (also used for kerning)
        int32_t pos = 0;
        uint16_t ch = 0;
        bool hasCh = false;
        if (lineLen > pos) {
            ch = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            hasCh = true;
        }

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);

            uint16_t nextCh = 0;
            bool hasNext = lineLen > pos;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != nullptr) {
                float advance = (float) glyph->shift;
                float leftFrac  = (lineWidth > 0.0f) ? (gradientX           / lineWidth) : 0.0f;
                float rightFrac = (lineWidth > 0.0f) ? ((gradientX + advance) / lineWidth) : 1.0f;
                int32_t c1 = Color_lerp(_c1, _c2, leftFrac);
                int32_t c2 = Color_lerp(_c1, _c2, rightFrac);
                int32_t c3 = Color_lerp(_c4, _c3, rightFrac);
                int32_t c4 = Color_lerp(_c4, _c3, leftFrac);

                bool drewSuccessfully = false;
                if (glyph->sourceWidth != 0 && glyph->sourceHeight != 0) {
                    float u0, v0, u1, v1;
                    float localX0, localY0;
                    GLuint glyphTexId;
                    int32_t glyphTpagIdx;

                    if (glResolveGlyph(gl, dw, &fontState, glyph, cursorX, cursorY, &glyphTexId, &glyphTpagIdx, &u0, &v0, &u1, &v1, &localX0, &localY0)) {
                        glBindTexture(GL_TEXTURE_2D, glyphTexId);
                        PS3_PALETTED_BEGIN(glyphTpagIdx);

                        float localX1 = localX0 + (float) glyph->sourceWidth;
                        float localY1 = localY0 + (float) glyph->sourceHeight;

                        // Transform corners
                        float px0, py0, px1, py1, px2, py2, px3, py3;
                        Matrix4f_transformPoint(&transform, localX0, localY0, &px0, &py0);
                        Matrix4f_transformPoint(&transform, localX1, localY0, &px1, &py1);
                        Matrix4f_transformPoint(&transform, localX1, localY1, &px2, &py2);
                        Matrix4f_transformPoint(&transform, localX0, localY1, &px3, &py3);

                        glBegin(GL_QUADS);
                            glColor4ub(BGR_R(c1), BGR_G(c1), BGR_B(c1), alpha * 255);
                            glTexCoord2f(u0, v0);
                            glVertex2f(px0, py0);

                            glColor4ub(BGR_R(c2), BGR_G(c2), BGR_B(c2), alpha * 255);
                            glTexCoord2f(u1, v0);
                            glVertex2f(px1, py1);

                            glColor4ub(BGR_R(c3), BGR_G(c3), BGR_B(c3), alpha * 255);
                            glTexCoord2f(u1, v1);
                            glVertex2f(px2, py2);

                            glColor4ub(BGR_R(c4), BGR_G(c4), BGR_B(c4), alpha * 255);
                            glTexCoord2f(u0, v1);
                            glVertex2f(px3, py3);
                        glEnd();
                        PS3_PALETTED_END();

                        drewSuccessfully = true;
                    }
                }

                cursorX += glyph->shift;
                gradientX   += glyph->shift;
                if (drewSuccessfully && hasNext) {
                    float kern = TextUtils_getKerningOffset(glyph, nextCh);
                    cursorX += kern;
                    gradientX   += kern;
                }
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        // Skip past the newline, treating \r\n and \n\r as single breaks
        if (textLen > lineEnd) {
            lineStart = TextUtils_skipNewline(text, lineEnd, textLen);
        } else {
            lineStart = lineEnd;
        }
    }
}

// ===[ Dynamic Sprite Creation/Deletion ]===

// Finds a free dynamic texture page slot (glTextures[i] == 0), or appends a new one.
static uint32_t findOrAllocTexturePageSlot(GLLegacyRenderer* gl) {
    // Scan dynamic range for a reusable slot
    for (uint32_t i = gl->originalTexturePageCount; gl->textureCount > i; i++) {
        if (gl->glTextures[i] == 0) return i;
    }
    // No free slot found, grow the arrays
    uint32_t newPageId = gl->textureCount;
    gl->textureCount++;
    gl->glTextures = (GLuint *)safeRealloc(gl->glTextures, gl->textureCount * sizeof(GLuint));
    gl->textureWidths = (int32_t *)safeRealloc(gl->textureWidths, gl->textureCount * sizeof(int32_t));
    gl->textureHeights = (int32_t *)safeRealloc(gl->textureHeights, gl->textureCount * sizeof(int32_t));
    gl->textureLoaded = (bool *)safeRealloc(gl->textureLoaded, gl->textureCount * sizeof(bool));
    // Dynamic sprites created from surfaces are real texture pages too. The
    // previous growth path extended only the four arrays above; draw/cache
    // code then indexed textureLastUsedFrame/texturePinned with the new page
    // ID and wrote past their original data.win-sized allocations. Chapter 4
    // ripplepuzzle grew 838 -> 840 pages, after which the pinned count became
    // corrupted (39 -> 76) and the next transition crashed in GXM.
    gl->textureLastUsedFrame = (uint32_t *)safeRealloc(
        gl->textureLastUsedFrame, gl->textureCount * sizeof(uint32_t));
    gl->texturePinned = (bool *)safeRealloc(
        gl->texturePinned, gl->textureCount * sizeof(bool));
    gl->glTextures[newPageId] = 0;
    gl->textureWidths[newPageId] = 0;
    gl->textureHeights[newPageId] = 0;
    gl->textureLoaded[newPageId] = false;
    gl->textureLastUsedFrame[newPageId] = 0;
    gl->texturePinned[newPageId] = false;
    return newPageId;
}

// Finds a free dynamic TPAG slot (texturePageId == -1), or appends a new one.
static uint32_t findOrAllocTpagSlot(DataWin* dw, uint32_t originalTpagCount) {
    for (uint32_t i = originalTpagCount; dw->tpag.count > i; i++) {
        if (dw->tpag.items[i].texturePageId == -1) return i;
    }
    uint32_t newIndex = dw->tpag.count;
    dw->tpag.count++;
    dw->tpag.items = (TexturePageItem *)safeRealloc(dw->tpag.items, dw->tpag.count * sizeof(TexturePageItem));
    memset(&dw->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dw->tpag.items[newIndex].texturePageId = -1;
    return newIndex;
}

static int32_t glCreateSpriteFromSurface(Renderer* renderer, int32_t surfaceID, int32_t x, int32_t y, int32_t w, int32_t h, bool removeback, bool smooth, int32_t xorig, int32_t yorig) {
    // TODO: implement these
    (void)smooth;
    (void)removeback;
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 >= w || 0 >= h) return -1;
    if (0 > surfaceID || (uint32_t) surfaceID >= gl->surfaceCount) return -1;
    if (gl->surfaces[surfaceID] == 0) return -1;

    // The Chapter 3 gameshow freezes the current application surface into a
    // sprite immediately before starting the battle.  A flush only submits the
    // commands; VitaGL can still return pixels from the preceding scene.  Wait
    // for this rare explicit readback so the snapshot contains the completed
    // stage rather than stale minigame/room contents.
    glFinish();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gl->surfaces[surfaceID]);

    uint8_t* pixels = (uint8_t *)safeMalloc((size_t) w * (size_t) h * 4);
    if (pixels == nullptr) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        return -1;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // Create a new GL texture from the captured pixels
    GLuint newTexId;
    glGenTextures(1, &newTexId);
    glBindTexture(GL_TEXTURE_2D, newTexId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((w & (w - 1)) == 0) && ((h & (h - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    free(pixels);

    // Find or allocate slots for texture page, TPAG, and sprite
    uint32_t pageId = findOrAllocTexturePageSlot(gl);
    gl->glTextures[pageId] = newTexId;
    gl->textureWidths[pageId] = w;
    gl->textureHeights[pageId] = h;
    gl->textureLoaded[pageId] = true;

    uint32_t tpagIndex = findOrAllocTpagSlot(dw, gl->originalTpagCount);
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t) w;
    tpag->sourceHeight = (uint16_t) h;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t) w;
    tpag->targetHeight = (uint16_t) h;
    tpag->boundingWidth = (uint16_t) w;
    tpag->boundingHeight = (uint16_t) h;
    tpag->texturePageId = (int16_t) pageId;

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, gl->originalSpriteCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    // name was set by DataWin_allocSpriteSlot ("__newsprite<N>"); don't overwrite it here
    sprite->width = (uint32_t) w;
    sprite->height = (uint32_t) h;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = (int32_t *)safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t) tpagIndex;
    sprite->maskCount = 0;
    sprite->masks = nullptr;

    fprintf(stderr, "GL: Created dynamic sprite %u (%dx%d) from surface at (%d,%d)\n", spriteIndex, w, h, x, y);
    return (int32_t) spriteIndex;
}

static void glDeleteSprite(Renderer* renderer, int32_t spriteIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;

    if (0 > spriteIndex || dw->sprt.count <= (uint32_t) spriteIndex) return;

    // Refuse to delete original data.win sprites
    if (gl->originalSpriteCount > (uint32_t) spriteIndex) {
        fprintf(stderr, "GL: Cannot delete data.win sprite %d\n", spriteIndex);
        return;
    }

    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return; // already deleted

    // Clean up GL texture and TPAG entries owned by this sprite.
    // Slots with index >= originalTpagCount are dynamically allocated and ours to free.
    repeat(sprite->textureCount, i) {
        int32_t tpagIdx = sprite->tpagIndices[i];
        if (tpagIdx >= 0 && (uint32_t) tpagIdx >= gl->originalTpagCount) {
            TexturePageItem* tpag = &dw->tpag.items[tpagIdx];
            int16_t pageId = tpag->texturePageId;
            if (pageId >= 0 && gl->textureCount > (uint32_t) pageId) {
                glDeleteTextures(1, &gl->glTextures[pageId]);
                gl->glTextures[pageId] = 0;
            }
            // Mark TPAG slot as free for reuse
            tpag->texturePageId = -1;
        }
    }

    // Clear the sprite entry so it won't be drawn and can be reused. Preserve `name` across the memset: the slot is still in sprt.count and must keep a valid string for asset_get_index / name lookups.
    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;

    fprintf(stderr, "GL: Deleted sprite %d\n", spriteIndex);
}

static BlendFactors glGpuGetBlendFactors(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*)renderer;
    return (BlendFactors){
        gl->currentSFactor, 
        gl->currentDFactor, 
        gl->currentSFactorAlpha, 
        gl->currentDFactorAlpha
    };
}

static int32_t glGpuGetBlendMode(Renderer* renderer) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    return gl->currentBlendMode;
}

static void glGpuSetBlendMode(Renderer* renderer, int32_t mode) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    
    gl->currentBlendMode = mode;
    gl->currentSFactor = GLCommon_blendFactorFromGL(GLCommon_blendModeToSFactor(mode));
    gl->currentDFactor = GLCommon_blendFactorFromGL(GLCommon_blendModeToDFactor(mode));
    gl->currentSFactorAlpha = gl->currentSFactor; 
    gl->currentDFactorAlpha = gl->currentDFactor;
    glBlendEquation(GLCommon_blendModeToEquation(mode));
    glBlendFunc(GLCommon_blendFactorToGL(gl->currentSFactor),
                GLCommon_blendFactorToGL(gl->currentDFactor));
}

static void glGpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor, int32_t sfactor_alpha, int32_t dfactor_alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    
    gl->currentBlendMode = bm_complex;
    gl->currentSFactor = sfactor;
    gl->currentDFactor = dfactor;
    gl->currentSFactorAlpha = sfactor_alpha;
    gl->currentDFactorAlpha = dfactor_alpha;
    
    glBlendFuncSeparate(
        GLCommon_blendFactorToGL(sfactor), 
        GLCommon_blendFactorToGL(dfactor), 
        GLCommon_blendFactorToGL(sfactor_alpha), 
        GLCommon_blendFactorToGL(dfactor_alpha)
    );
}

static void glGpuSetBlendEnable(Renderer* renderer, bool enable) {
    (void)renderer;
    enable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
}

static bool glGpuGetBlendEnable(MAYBE_UNUSED Renderer* renderer) {
    
    return glIsEnabled(GL_BLEND);
}

static void glGpuSetAlphaTestEnable(MAYBE_UNUSED Renderer* renderer, bool enable) {
#ifdef PLATFORM_VITA
    GLLegacyRenderer* gl = (GLLegacyRenderer*)renderer;
    gl->alphaTestEnable = enable;
    // Alpha test discards fragments; it must not change blend factors.
    // Rhythm notes and Knight attacks write a destination-alpha mask first.
    // Substituting SRC_ALPHA destroys that independent mask. VitaGL's FFP
    // shader already implements the discard through alpha_op/ALPHA_CUT_UNIF.
#endif
    enable ? glEnable(GL_ALPHA_TEST) : glDisable(GL_ALPHA_TEST);
}

static void glGpuSetAlphaTestRef(MAYBE_UNUSED Renderer* renderer, uint8_t ref) {
    glAlphaFunc(GL_GREATER, ref/255.0f);
}

static void glGpuSetColorWriteEnable(Renderer* renderer, bool red, bool green, bool blue, bool alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    gl->colorWriteR = red;
    gl->colorWriteG = green;
    gl->colorWriteB = blue;
    gl->colorWriteA = alpha;
    glColorMask(red, green, blue, alpha);
}

static void glGpuGetColorWriteEnable(Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    *red = gl->colorWriteR;
    *green = gl->colorWriteG;
    *blue = gl->colorWriteB;
    *alpha = gl->colorWriteA;
}

// ===[ Surfaces ]===

static int32_t glLegacyCreateSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    // Save the current FBO binding so creating a surface doesn't change the active render target.
    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    uint32_t surfaceIndex = GLCommon_findOrAllocateSurfaceSlot(&gl->surfaces, &gl->surfaceTexture, &gl->surfaceWidth, &gl->surfaceHeight, &gl->surfaceCount);

    int32_t texW = gl->needsPOT ? nextPow2(width)  : width;
    int32_t texH = gl->needsPOT ? nextPow2(height) : height;

    glGenFramebuffers(1, &gl->surfaces[surfaceIndex]);
    glGenTextures(1, &gl->surfaceTexture[surfaceIndex]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex]);
#ifdef PLATFORM_VITA
    // The final Flowery fight creates close to fifty intermediate surfaces.
    // RGBA8 consumed 25-27 MiB and forced the BC3 atlas working set into an
    // eviction/re-upload loop. These are effect/composition targets rather
    // than source art, so RGBA4444 preserves alpha while halving their VRAM.
    bool floweryStructuralSurface = vitaFloweryBattleMemoryMode &&
        VitaGpuRules_floweryStructuralSurface(width, height);
    bool compactEffectSurface = (vitaFloweryBattleMemoryMode && !floweryStructuralSurface) ||
                                vitaChapter3PerformanceSurfaceMode;
    GLenum surfaceInternal = compactEffectSurface ? GL_RGBA4 : GL_RGBA;
    GLenum surfaceType = compactEffectSurface ?
                         GL_UNSIGNED_SHORT_4_4_4_4 : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, surfaceInternal, texW, texH, 0,
                 GL_RGBA, surfaceType, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((texW & (texW - 1)) == 0) && ((texH & (texH - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceIndex]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceIndex], 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "GL: Surface FBO incomplete (status=0x%X)\n", status);
    }

    gl->surfaceWidth[surfaceIndex] = width;
    gl->surfaceHeight[surfaceIndex] = height;

    fprintf(stderr, "GL: Created surface %u with size (%dx%d)\n", surfaceIndex, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);
    return (int32_t) surfaceIndex;
}

static int32_t glLegacyEnsureApplicationSurface(Renderer* renderer, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    int32_t id = renderer->runner->applicationSurfaceId;

    bool needsCreate = (id < 0) || ((uint32_t) id >= gl->surfaceCount) || (gl->surfaces[id] == 0);
    if (needsCreate) {
        id = glLegacyCreateSurface(renderer, width, height);
        // Publish immediately so anything that re-queries the runner during this frame sees the new ID.
        renderer->runner->applicationSurfaceId = id;
        return id;
    }

    if (gl->surfaceWidth[id] != width || gl->surfaceHeight[id] != height) {
        renderer->vtable->surfaceResize(renderer, id, width, height);
    }
    return id;
}

static bool glLegacySurfaceExists(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    return gl->surfaces[surfaceId] != 0;
}

static float glLegacyGetSurfaceWidth(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceWidth[surfaceId];
}

static float glLegacyGetSurfaceHeight(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return 0.0f;
    if (gl->surfaces[surfaceId] == 0) return 0.0f;
    return (float) gl->surfaceHeight[surfaceId];
}

static void glLegacySurfaceResize(Renderer* renderer, int32_t surfaceId, int32_t width, int32_t height) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    if (gl->surfaces[surfaceId] == 0) return;
    if (gl->surfaceWidth[surfaceId] == width && gl->surfaceHeight[surfaceId] == height) return;

    GLint prevBinding = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevBinding);

    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);

    int32_t texW = gl->needsPOT ? nextPow2(width)  : width;
    int32_t texH = gl->needsPOT ? nextPow2(height) : height;

    glGenTextures(1, &gl->surfaceTexture[surfaceId]);
    glBindTexture(GL_TEXTURE_2D, gl->surfaceTexture[surfaceId]);
#ifdef PLATFORM_VITA
    bool floweryStructuralSurface = vitaFloweryBattleMemoryMode &&
        VitaGpuRules_floweryStructuralSurface(width, height);
    bool compactEffectSurface = ((vitaFloweryBattleMemoryMode && !floweryStructuralSurface) ||
                                 vitaChapter3PerformanceSurfaceMode) &&
                                surfaceId != renderer->runner->applicationSurfaceId;
    GLenum surfaceInternal = compactEffectSurface ? GL_RGBA4 : GL_RGBA;
    GLenum surfaceType = compactEffectSurface ?
                         GL_UNSIGNED_SHORT_4_4_4_4 : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, surfaceInternal, texW, texH, 0,
                 GL_RGBA, surfaceType, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bool is_pot = ((texW & (texW - 1)) == 0) && ((texH & (texH - 1)) == 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, is_pot ? GL_REPEAT : GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->surfaceTexture[surfaceId], 0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) prevBinding);

    gl->surfaceWidth[surfaceId] = width;
    gl->surfaceHeight[surfaceId] = height;
    fprintf(stderr, "GL: Resized Surface %u to (%dx%d)\n", surfaceId, width, height);
}

static void glLegacySurfaceFree(Renderer* renderer, int32_t surfaceId) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return;
    // Freeing the application_surface is a no-op from GML; the runner manages its lifecycle via application_surface_enable.
    if (surfaceId == renderer->runner->applicationSurfaceId) return;
    if (gl->surfaceTexture[surfaceId] != 0) glDeleteTextures(1, &gl->surfaceTexture[surfaceId]);
    if (gl->surfaces[surfaceId] != 0) glDeleteFramebuffers(1, &gl->surfaces[surfaceId]);
    gl->surfaces[surfaceId] = 0;
    gl->surfaceTexture[surfaceId] = 0;
    gl->surfaceWidth[surfaceId] = 0;
    gl->surfaceHeight[surfaceId] = 0;
#ifdef PLATFORM_VITA
    // Room effects commonly destroy several large surfaces during Room End.
    // Make those backing allocations available before destination preloading.
    glFinish();
    vglForceGarbageCollection();
#endif
    fprintf(stderr, "GL: Freed Surface %d\n", surfaceId);
}

static bool glLegacySetRenderTarget(Renderer* renderer, int32_t surfaceId, bool implicitApplicationSurface) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;

    int32_t viewCurrent = 0;
    if (renderer->runner->viewsEnabled) {
    viewCurrent = renderer->runner->viewCurrent;
    }
    RuntimeView* view = &renderer->runner->views[viewCurrent];
    gl->base.cameraCurrent = view->cameraId;
    GMLCamera* camera = Runner_getCameraById(renderer->runner, gl->base.cameraCurrent);

    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;

    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[surfaceId]);

    if (surfaceId == renderer->runner->applicationSurfaceId && implicitApplicationSurface) {
        glViewport(gl->base.CPortX, gl->base.CPortY, gl->base.CPortW, gl->base.CPortH);
        glEnable(GL_SCISSOR_TEST);
        glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
        return true;
    }

    if (surfaceId == view->surfaceId) {
    //the surface belongs to the view we are rending, we use the view's camera.
    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);
    glApplyProjection(renderer,&camera->viewMatrix,&camera->projectionMatrix);
    return true;
    } else {
    //camera will use full surface.
    gl->base.cameraCurrent = SURFACE_CAMERA;
    GMLCamera* camera =  &renderer->runner->surfaceCamera;

    camera->allocated = true;
    camera->viewX = 0.0;
    camera->viewY = 0.0;
    camera->viewWidth = gl->surfaceWidth[surfaceId];
    camera->viewHeight = gl->surfaceHeight[surfaceId];
    camera->borderX = 0;
    camera->borderY = 0;
    camera->speedX = 0;
    camera->speedY = 0;
    camera->objectId = -1;
    camera->viewAngle = 0;
    Runner_updateCameraViewSimple(camera);

    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);
    glApplyProjection(renderer, &camera->viewMatrix,&camera->projectionMatrix);
    return true;
    }


    glViewport(0, 0, gl->surfaceWidth[surfaceId], gl->surfaceHeight[surfaceId]);
    glDisable(GL_SCISSOR_TEST);

    return true;
}

// Resolves a surfaceID to a GL texture and its actual texture size
// (POT dimensions if needsPOT, logical dimensions otherwise).
static bool resolveSurfaceTexture(GLLegacyRenderer* gl, int32_t surfaceId, GLuint* outTexId, int32_t* outTexW, int32_t* outTexH) {
    if (0 > surfaceId || (uint32_t) surfaceId >= gl->surfaceCount) return false;
    if (gl->surfaces[surfaceId] == 0) return false;
    *outTexId = gl->surfaceTexture[surfaceId];
    if (gl->needsPOT) {
        *outTexW = nextPow2(gl->surfaceWidth[surfaceId]);
        *outTexH = nextPow2(gl->surfaceHeight[surfaceId]);
    } else {
        *outTexW = gl->surfaceWidth[surfaceId];
        *outTexH = gl->surfaceHeight[surfaceId];
    }
    return true;
}

static void glLegacyDrawSurfaceTiled(MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float roomW, MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    // No-op
}

static void glLegacyDrawSurface(Renderer* renderer, int32_t surfaceId, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t color, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLuint texId;
    int32_t texW, texH;
    if (!resolveSurfaceTexture(gl, surfaceId, &texId, &texW, &texH)) return;

    // Use the logical surface size for the default "draw everything" case,
    // not the POT texture dimensions (texW/texH may be rounded up).
    if (0 > srcWidth) {
        srcLeft = 0;
        srcTop = 0;
        srcWidth = gl->surfaceWidth[surfaceId];
        srcHeight = gl->surfaceHeight[surfaceId];
    }

    // top-down GML coords -> flipped V for our bottom-up texture
    float u0 = (float) srcLeft / (float) texW;
    float u1 = (float) (srcLeft + srcWidth) / (float) texW;
#ifndef PLATFORM_PS3
    float v0 = (float) srcTop / (float) texH;
    float v1 = (float) (srcTop + srcHeight) / (float) texH;
#else
    float v1 = (float) srcTop / (float) texH;
    float v0 = (float) (srcTop + srcHeight) / (float) texH;
#endif

    float r = (float) BGR_R(color) / 255.0f;
    float g = (float) BGR_G(color) / 255.0f;
    float b = (float) BGR_B(color) / 255.0f;

    float angleRad = -angleDeg * ((float) M_PI / 180.0f);
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, angleRad);

    float x0, y0, x1, y1, x2, y2, x3, y3;
    Matrix4f_transformPoint(&transform, 0.0f,             0.0f,             &x0, &y0);
    Matrix4f_transformPoint(&transform, (float) srcWidth, 0.0f,             &x1, &y1);
    Matrix4f_transformPoint(&transform, (float) srcWidth, (float) srcHeight, &x2, &y2);
    Matrix4f_transformPoint(&transform, 0.0f,             (float) srcHeight, &x3, &y3);

    glBindTexture(GL_TEXTURE_2D, texId);
    glBegin(GL_QUADS);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v0); glVertex2f(x1, y1);
        glColor4f(r, g, b, alpha); glTexCoord2f(u1, v1); glVertex2f(x2, y2);
        glColor4f(r, g, b, alpha); glTexCoord2f(u0, v1); glVertex2f(x3, y3);
    glEnd();
}

static void glLegacyDrawSurfaceColor(Renderer* renderer, int32_t surfaceId, int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale, float yscale, float angleDeg, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, float alpha) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLuint texId; int32_t texW, texH;
    if (!resolveSurfaceTexture(gl, surfaceId, &texId, &texW, &texH)) return;
    if (srcWidth < 0) { srcLeft = srcTop = 0; srcWidth = gl->surfaceWidth[surfaceId]; srcHeight = gl->surfaceHeight[surfaceId]; }
    float u0 = (float) srcLeft / texW, u1 = (float) (srcLeft + srcWidth) / texW;
    float v0 = (float) srcTop / texH, v1 = (float) (srcTop + srcHeight) / texH;
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, -angleDeg * ((float) M_PI / 180.0f));
    float x0,y0,x1,y1,x2,y2,x3,y3;
    Matrix4f_transformPoint(&transform, 0, 0, &x0, &y0);
    Matrix4f_transformPoint(&transform, srcWidth, 0, &x1, &y1);
    Matrix4f_transformPoint(&transform, srcWidth, srcHeight, &x2, &y2);
    Matrix4f_transformPoint(&transform, 0, srcHeight, &x3, &y3);
    glBindTexture(GL_TEXTURE_2D, texId); glBegin(GL_QUADS);
#define VTX(C,U,V,X,Y) glColor4f(BGR_R(C)/255.0f,BGR_G(C)/255.0f,BGR_B(C)/255.0f,alpha); glTexCoord2f(U,V); glVertex2f(X,Y)
    VTX(c1,u0,v0,x0,y0); VTX(c2,u1,v0,x1,y1); VTX(c3,u1,v1,x2,y2); VTX(c4,u0,v1,x3,y3);
#undef VTX
    glEnd();
}

static void glLegacySurfaceCopy(Renderer* renderer, int32_t destSurfaceID, int32_t destX, int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, bool part) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    GLCommon_surfaceBlit(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, destSurfaceID, destX, destY, srcSurfaceID, srcX, srcY, srcW, srcH, part);
}

static bool glLegacySurfaceGetPixels(Renderer* renderer, int32_t surfaceId, uint8_t* outRGBA) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    return GLCommon_surfaceGetPixels(gl->surfaces, gl->surfaceWidth, gl->surfaceHeight, gl->surfaceCount, surfaceId, outRGBA);
}


// ===[ Vtable ]===

// Decode a texture handle produced by glSpriteGetTexture back into its tpag and page dimensions.
// Returns false for the 0 ("no texture") handle or an unresolvable one.
static bool glLegacyResolveTextureHandle(GLLegacyRenderer* gl, uint32_t texHandle, TexturePageItem** outTpag, int32_t* outW, int32_t* outH) {
    if (texHandle == 0) return false;
    if (texHandle & GL_SURFACE_TEXTURE_FLAG) {
        uint32_t sid = texHandle & ~GL_SURFACE_TEXTURE_FLAG;
        if (sid >= gl->surfaceCount || gl->surfaceTexture[sid] == 0) return false;
        if (outTpag) *outTpag = nullptr;
        *outW = gl->surfaceWidth[sid];
        *outH = gl->surfaceHeight[sid];
        return true;
    }
    DataWin* dw = gl->base.dataWin;
    int32_t tpagIndex = (int32_t) texHandle - 1;
    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return false;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return false;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return false;
    *outTpag = tpag;
    *outW = gl->textureWidths[pageId];
    *outH = gl->textureHeights[pageId];
    return true;
}

static uint32_t glSpriteGetTexture(Renderer* renderer, int32_t tpagIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    DataWin* dw = renderer->dataWin;
    if (0 > tpagIndex || dw->tpag.count <= (uint32_t) tpagIndex) return 0;
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    int16_t pageId = tpag->texturePageId;
    if (0 > pageId || gl->textureCount <= (uint32_t) pageId) return 0;
    if (!GLLegacyRenderer_ensureTextureLoaded(gl, (uint32_t) pageId)) return 0;
    return (uint32_t) (tpagIndex + 1);
}

static uint32_t glSurfaceGetTexture(Renderer* renderer, int32_t surfaceID) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (surfaceID < 0 || (uint32_t) surfaceID >= gl->surfaceCount) return 0;
    if (gl->surfaceTexture[surfaceID] == 0) return 0;
    return GL_SURFACE_TEXTURE_FLAG | (uint32_t) surfaceID;
}

static float glTextureGetTexelWidth(Renderer* renderer, uint32_t texHandle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= w) return 1.0f;
    return 1.0f / (float) w;
}

static float glTextureGetTexelHeight(Renderer* renderer, uint32_t texHandle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= h) return 1.0f;
    return 1.0f / (float) h;
}

static bool glTextureGetUVs(Renderer* renderer, uint32_t texHandle, float* outUVs) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    TexturePageItem* tpag;
    int32_t w = 0, h = 0;
    if (!glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h) || 0 >= w || 0 >= h) return false;
    // Surface handles cover the whole texture (no tpag sub-region).
    if (tpag == nullptr) {
        outUVs[0] = 0.0f; outUVs[1] = 0.0f; outUVs[2] = 1.0f; outUVs[3] = 1.0f;
        return true;
    }
    float divW = 1.0f / (float) w;
    float divH = 1.0f / (float) h;
    outUVs[0] = (float) tpag->sourceX * divW;                       // left
    outUVs[1] = (float) tpag->sourceY * divH;                       // top
    outUVs[2] = outUVs[0] + (float) tpag->sourceWidth * divW;       // right
    outUVs[3] = outUVs[1] + (float) tpag->sourceHeight * divH;      // bottom
    return true;
}

// Bind a texture into the stage returned by glShaderGetSamplerIndex.
static void glTextureSetStage(Renderer* renderer, int32_t slot, uint32_t texHandle) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
    if (slot < 0 || slot >= 8) return; // up to 8 texture units

    TexturePageItem* tpag = nullptr;
    int32_t w = 0, h = 0;
    GLuint glid = 0;
    if (glLegacyResolveTextureHandle(gl, texHandle, &tpag, &w, &h)) {
        if (tpag != nullptr) {
            glid = gl->glTextures[tpag->texturePageId];
        } else if (texHandle & GL_SURFACE_TEXTURE_FLAG) {
            uint32_t sid = texHandle & ~GL_SURFACE_TEXTURE_FLAG;
            if (sid < gl->surfaceCount) glid = gl->surfaceTexture[sid];
        }
    }

    glActiveTexture(GL_TEXTURE0 + slot);
    if (glid != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, glid);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);        // restore unit 0 for the fixed pipeline
}

#ifdef PLATFORM_VITA
// libshacccg rejects compound multiplication in a number of GameMaker GLSL ES
// shaders. Rewrite simple lvalue *= expression statements before compilation.
// This mirrors the proven Vita shader compatibility path used upstream.
static char* vitaFixShaderSource(const char* src) {
    if (src == NULL) return NULL;
    size_t srcLen = strlen(src);
    size_t capacity = srcLen * 2U + 64U;
    char* out = malloc(capacity);
    if (out == NULL) return NULL;
    size_t outLen = 0;

#define VITA_SHADER_ENSURE(n) do { \
    if (outLen + (size_t)(n) + 1U > capacity) { \
        capacity = (outLen + (size_t)(n) + 1U) * 2U; \
        char* grown = realloc(out, capacity); \
        if (grown == NULL) { free(out); return NULL; } \
        out = grown; \
    } \
} while (0)
#define VITA_SHADER_APPEND(p, n) do { \
    VITA_SHADER_ENSURE(n); memcpy(out + outLen, (p), (n)); outLen += (n); \
} while (0)

    size_t i = 0;
    while (i < srcLen) {
        if (src[i] == '/' && i + 1U < srcLen && src[i + 1U] == '/') {
            size_t start = i;
            while (i < srcLen && src[i] != '\n') i++;
            VITA_SHADER_APPEND(src + start, i - start);
            continue;
        }
        if (src[i] == '/' && i + 1U < srcLen && src[i + 1U] == '*') {
            size_t start = i;
            i += 2U;
            while (i + 1U < srcLen && !(src[i] == '*' && src[i + 1U] == '/')) i++;
            i = i + 1U < srcLen ? i + 2U : srcLen;
            VITA_SHADER_APPEND(src + start, i - start);
            continue;
        }
        if (isalpha((unsigned char)src[i]) || src[i] == '_') {
            size_t lhsStart = i;
            size_t cursor = i;
            while (cursor < srcLen && (isalnum((unsigned char)src[cursor]) || src[cursor] == '_')) cursor++;
            for (;;) {
                size_t saved = cursor;
                while (cursor < srcLen && isspace((unsigned char)src[cursor])) cursor++;
                if (cursor < srcLen && src[cursor] == '.') {
                    cursor++;
                    while (cursor < srcLen && isspace((unsigned char)src[cursor])) cursor++;
                    if (cursor >= srcLen || (!isalpha((unsigned char)src[cursor]) && src[cursor] != '_')) {
                        cursor = saved; break;
                    }
                    while (cursor < srcLen && (isalnum((unsigned char)src[cursor]) || src[cursor] == '_')) cursor++;
                } else if (cursor < srcLen && src[cursor] == '[') {
                    int depth = 1;
                    cursor++;
                    while (cursor < srcLen && depth > 0) {
                        if (src[cursor] == '[') depth++;
                        else if (src[cursor] == ']') depth--;
                        cursor++;
                    }
                    if (depth != 0) { cursor = saved; break; }
                } else {
                    cursor = saved; break;
                }
            }
            size_t op = cursor;
            while (op < srcLen && isspace((unsigned char)src[op])) op++;
            if (op + 1U < srcLen && src[op] == '*' && src[op + 1U] == '=') {
                size_t rhs = op + 2U;
                size_t end = rhs;
                int depth = 0;
                while (end < srcLen) {
                    if (src[end] == '(' || src[end] == '[') depth++;
                    else if (src[end] == ')' || src[end] == ']') depth--;
                    else if (src[end] == ';' && depth == 0) break;
                    end++;
                }
                if (end < srcLen) {
                    while (rhs < end && isspace((unsigned char)src[rhs])) rhs++;
                    size_t rhsEnd = end;
                    while (rhsEnd > rhs && isspace((unsigned char)src[rhsEnd - 1U])) rhsEnd--;
                    VITA_SHADER_APPEND(src + lhsStart, cursor - lhsStart);
                    VITA_SHADER_APPEND(" = ", 3U);
                    VITA_SHADER_APPEND(src + lhsStart, cursor - lhsStart);
                    VITA_SHADER_APPEND(" * (", 4U);
                    VITA_SHADER_APPEND(src + rhs, rhsEnd - rhs);
                    VITA_SHADER_APPEND(");", 2U);
                    i = end + 1U;
                    continue;
                }
            }
        }
        VITA_SHADER_APPEND(src + i, 1U);
        i++;
    }
    VITA_SHADER_ENSURE(0);
    out[outLen] = '\0';
#undef VITA_SHADER_APPEND
#undef VITA_SHADER_ENSURE
    return out;
}
#endif

// Compile every GameMaker shader in data.win into a GL program ahead of time.
// Only shaders with a compact, audited Legacy-GL vertex path are compiled on
// Vita.  GameMaker's generated vertex programs carry the complete lighting
// pipeline and five matrices even for a 2D sprite; compiling that whole table
// fragments ShaRK/GXM memory and was the source of the old Chapter 5 crashes.
// The fragment stage is still the shader shipped by the game, so its visual
// result and uniforms remain compatible with the original GML.
static bool legacyCompactShaderSupported(const char* name) {
    if (name == NULL) return false;
    static const char* supported[] = {
        // Shared colour/palette effects.
        "shd_hue", "shd_hsl", "shd_hsv_transform", "shd_pal_swapper",
        "shd_saturation", "sh_saturation",
        // Chapter 3: these effects all use the regular 2D texture/colour
        // contract and are compiled only when first selected. Keep video_yuv
        // on the native video path; it has a different multi-plane contract.
        "shd_channel_change", "shd_channel_change_alt",
        "shd_channel_change_backup", "shd_channel_change_sprite",
        // shd_crt samples beyond the small puzzle-monitor surface and its
        // Vita Legacy adaptation can resolve to a black monitor. The regular
        // textured path preserves the complete, correctly centred minigame.
        "shd_chromakey", "shd_grayscale",
        "shd_grayscale_pal_swapper",
        "shd_grayscalesand", "shd_grayscalesand_tolerance",
        "shd_hue_rotation", "shd_invert", "shd_luminosity_to_transparency",
        "shd_rainbow", "shd_rgb_hsv_shift", "shd_rgb_to_hsv",
        "shd_rgb_yiq_shift", "shd_tone",
        // Chapter 4's visible 2D effects.
        "shd_fountaineffect", "shd_fountaineffect_alt", "shd_fountainsurface",
        "shd_prophecy", "shd_prophecy_legend", "shd_ripple",
        // Chapter 5 logo and Light World town shadows.
        "shd_shoujo", "shd_shadowblend", "shd_shadowblend_evening",
        "shd_forcecolour", "shd_forcefullalpha"
    };
    for (uint32_t i = 0; i < sizeof(supported) / sizeof(supported[0]); ++i) {
        if (strcmp(name, supported[i]) == 0) return true;
    }
    return false;
}

#ifdef PLATFORM_VITA
bool GLLegacyRenderer_experimentalShadersEnabled(const GLLegacyRenderer* gl) {
    extern bool g_vitaExperimentalGlActive;
    extern int g_vitaShadersEnabled;
    return gl != nullptr && g_vitaExperimentalGlActive && g_vitaShadersEnabled;
}

static void vitaLogExperimentalShader(GLLegacyRenderer* gl, const char* outcome,
                                      uint32_t shaderIndex, const char* reason) {
    if (!GLLegacyRenderer_experimentalShadersEnabled(gl)) return;
    extern void VitaProbe_logLine(const char* text);
    const char* name = "?";
    if (gl->base.dataWin != nullptr && shaderIndex < gl->base.dataWin->shdr.count &&
        gl->base.dataWin->shdr.shaders[shaderIndex].name != nullptr)
        name = gl->base.dataWin->shdr.shaders[shaderIndex].name;
    char line[224];
    snprintf(line, sizeof(line), "EXPERIMENTAL_SHADER=%s index=%u name=%s reason=%s",
             outcome, shaderIndex, name, reason != nullptr ? reason : "none");
    VitaProbe_logLine(line);
}
#endif

#ifdef PLATFORM_VITA
static int32_t g_vitaShaderCompileOnly = -1;
static uint32_t glCompileAllShaders(GLLegacyRenderer* gl, VitaShaderPrepareProgress progress, void* user) {
#else
static void glCompileAllShaders(GLLegacyRenderer* gl) {
#endif
    DataWin* dw = gl->base.dataWin;
#ifdef PLATFORM_VITA
    if (!gl->shaderPrograms) return 0;
    uint32_t totalSupported = 0;
    uint32_t processed = 0;
    uint32_t compiledCount = 0;
    for (uint32_t i = 0; i < dw->shdr.count; ++i) {
        if (g_vitaShaderCompileOnly >= 0 && (uint32_t)g_vitaShaderCompileOnly != i) continue;
        Shader* candidate = &dw->shdr.shaders[i];
        if (!candidate->present || candidate->name == nullptr) continue;
        if (legacyCompactShaderSupported(candidate->name)) totalSupported++;
    }
    if (progress) progress(0, totalSupported, user);
#else
    if (!gl->shaderPrograms) return;
#endif

    for (uint32_t shaderIndex = 0; shaderIndex < dw->shdr.count; shaderIndex++) {
#ifdef PLATFORM_VITA
        if (g_vitaShaderCompileOnly >= 0 && (uint32_t)g_vitaShaderCompileOnly != shaderIndex) continue;
#endif
        Shader* sh = &dw->shdr.shaders[shaderIndex];
        if (!sh->present) continue;
        const char* shaderName = sh->name != nullptr ? sh->name : "";
#ifdef PLATFORM_VITA
        // Compiling the complete GameMaker shader table fragments GXM memory
        // and several generated shaders are outside libshacccg's supported
        // GLSL subset. Keep a small audited allow-list for effects whose
        // fixed-pipeline fallback is visibly incorrect.
        // Compiling arbitrary embedded GameMaker programs can still abort in
        // SceGxm.  Keep this list tied to the compact compatibility programs
        // below; unsupported effects deliberately use the fixed pipeline.
        bool supported = legacyCompactShaderSupported(shaderName);
        if (!supported) continue;
        if (progress) progress(processed, totalSupported, user);
        processed++;
        if (gl->shaderPrograms[shaderIndex] != 0) {
            compiledCount++;
            continue;
        }
        if (gl->shaderAttempted[shaderIndex]) continue;
#endif
        gl->shaderAttempted[shaderIndex] = true;

#ifdef PLATFORM_VITA
        {
            extern void VitaProbe_logLine(const char* text);
            char shaderBegin[160];
            snprintf(shaderBegin, sizeof(shaderBegin), "SHADER_COMPILE=begin index=%u name=%s",
                     shaderIndex, shaderName[0] ? shaderName : "?");
            VitaProbe_logLine(shaderBegin);
        }
#endif

#ifdef PLATFORM_VITA
        const char* vsrc = sh->glslES_Vertex ? sh->glslES_Vertex : sh->glsl_Vertex;
        const char* fsrc = sh->glslES_Fragment ? sh->glslES_Fragment : sh->glsl_Fragment;
#else
        // Desktop legacy validation runs on desktop OpenGL; compiling the ES
        // source here fails on precision qualifiers and silently exercises the
        // fixed-pipeline fallback instead of the shader under test.
        const char* vsrc = sh->glsl_Vertex ? sh->glsl_Vertex : sh->glslES_Vertex;
        const char* fsrc = sh->glsl_Fragment ? sh->glsl_Fragment : sh->glslES_Fragment;
#endif
        if (!vsrc || !fsrc) continue;

        // The generated GameMaker vertex shader addresses a five-element
        // gm_Matrices array. VitaGL/libshacccg links it, but the legacy
        // immediate-mode stream receives a zero WVP entry and all hue-shifted
        // Swatchlings disappear. Use the same YIQ operation with a compact,
        // explicit MVP uniform. The desktop form is intentionally equivalent
        // so local-test exercises precisely this path before the Vita build.
        static const char* hueVertexDesktop =
            "#version 120\n"
            "attribute vec3 in_Position; attribute vec4 in_Colour; attribute vec2 in_TextureCoord;\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; }\n";
        static const char* hueFragmentDesktop =
            "#version 120\n"
            "uniform sampler2D gm_BaseTexture; uniform float u_Position; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "const mat3 rgb2yiq=mat3(0.299,0.587,0.114,0.595716,-0.274453,-0.321263,0.211456,-0.522591,0.311135);\n"
            "const mat3 yiq2rgb=mat3(1.0,0.9563,0.6210,1.0,-0.2721,-0.6474,1.0,-1.1070,1.7046);\n"
            "void main(){ vec4 t=texture2D(gm_BaseTexture,v_vTexcoord); vec3 y=(t.rgb*v_vColour.rgb)*rgb2yiq; float h=atan(y.b,y.g)+u_Position; float c=sqrt(y.b*y.b+y.g*y.g); gl_FragColor=vec4(vec3(y.r,c*cos(h),c*sin(h))*yiq2rgb,t.a*v_vColour.a); }\n";
        static const char* paletteVertexDesktop =
            "#version 120\n"
            "#define in_Position gl_Vertex.xyz\n#define in_Colour gl_Color\n#define in_TextureCoord gl_MultiTexCoord0.xy\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; }\n";
        static const char* paletteFragmentDesktop =
            "#version 120\n"
            "uniform sampler2D gm_BaseTexture; uniform sampler2D u_palTexture; uniform vec4 u_Uvs; uniform float u_paletteId; uniform vec2 u_pixelSize;\n"
            "varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 src=texture2D(gm_BaseTexture,v_vTexcoord); if(src.a<=0.0){ gl_FragColor=vec4(0.0); return; } vec4 outc=src;"
            "for(int i=0;i<256;i++){ vec2 p=u_Uvs.xy+vec2(0.0,float(i)*u_pixelSize.y); if(p.y>=u_Uvs.w)break; vec4 key=texture2D(u_palTexture,p);"
            "if(distance(key,src)<=0.004){ float col=floor(u_paletteId+1.0); vec2 q=vec2(u_Uvs.x+u_pixelSize.x*col,p.y);"
            "outc=mix(texture2D(u_palTexture,q-vec2(u_pixelSize.x,0.0)),texture2D(u_palTexture,q),fract(u_paletteId)); break; }}"
            "gl_FragColor=v_vColour*outc; }\n";
        static const char* compactVertexDesktop =
            "#version 120\n"
            "#define in_Position gl_Vertex.xyz\n#define in_Colour gl_Color\n#define in_TextureCoord gl_MultiTexCoord0.xy\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour; varying vec2 v_vPosition;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; v_vPosition=in_Position.xy; }\n";
        static const char* shadowFragmentDesktop =
            "#version 120\n"
            "uniform sampler2D gm_BaseTexture; uniform vec4 shadowCol; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 c=v_vColour*texture2D(gm_BaseTexture,v_vTexcoord); c.rgb=shadowCol.rgb; if(c.a>0.0)c.a=min(shadowCol.a,c.a); gl_FragColor=c; }\n";
        static const char* shadowEveningFragmentDesktop =
            "#version 120\n"
            "uniform sampler2D gm_BaseTexture; uniform vec4 shadowCol; uniform vec2 pixelw; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 c=v_vColour*texture2D(gm_BaseTexture,v_vTexcoord); float px=v_vTexcoord.x-pixelw.x*6.0; if(px<pixelw.y||texture2D(gm_BaseTexture,vec2(px,v_vTexcoord.y)).a<=0.0){c.rgb=shadowCol.rgb;if(c.a>0.0)c.a=min(shadowCol.a,c.a);} gl_FragColor=c; }\n";
#ifdef PLATFORM_VITA
        static const char* hueVertexVita =
            "attribute vec3 in_Position; attribute vec4 in_Colour; attribute vec2 in_TextureCoord;\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; }\n";
        static const char* hueFragmentVita =
            "precision mediump float; uniform sampler2D gm_BaseTexture; uniform float u_Position; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "const mat3 rgb2yiq=mat3(0.299,0.587,0.114,0.595716,-0.274453,-0.321263,0.211456,-0.522591,0.311135);\n"
            "const mat3 yiq2rgb=mat3(1.0,0.9563,0.6210,1.0,-0.2721,-0.6474,1.0,-1.1070,1.7046);\n"
            "void main(){ vec4 t=texture2D(gm_BaseTexture,v_vTexcoord); vec3 y=(t.rgb*v_vColour.rgb)*rgb2yiq; float h=atan(y.b,y.g)+u_Position; float c=sqrt(y.b*y.b+y.g*y.g); gl_FragColor=vec4(vec3(y.r,c*cos(h),c*sin(h))*yiq2rgb,t.a*v_vColour.a); }\n";
        static const char* paletteVertexVita =
            "attribute vec3 in_Position; attribute vec4 in_Colour; attribute vec2 in_TextureCoord;\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; }\n";
        static const char* paletteFragmentVita =
            "precision mediump float; uniform sampler2D gm_BaseTexture; uniform sampler2D u_palTexture; uniform vec4 u_Uvs; uniform float u_paletteId; uniform vec2 u_pixelSize;\n"
            "varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 src=texture2D(gm_BaseTexture,v_vTexcoord); if(src.a<=0.0){ gl_FragColor=vec4(0.0); return; } vec4 outc=src;"
            "for(int i=0;i<256;i++){ vec2 p=u_Uvs.xy+vec2(0.0,float(i)*u_pixelSize.y); if(p.y>=u_Uvs.w)break; vec4 key=texture2D(u_palTexture,p);"
            "if(distance(key,src)<=0.004){ float col=floor(u_paletteId+1.0); vec2 q=vec2(u_Uvs.x+u_pixelSize.x*col,p.y);"
            "outc=mix(texture2D(u_palTexture,q-vec2(u_pixelSize.x,0.0)),texture2D(u_palTexture,q),fract(u_paletteId)); break; }}"
            "gl_FragColor=v_vColour*outc; }\n";
        // Common 2D GameMaker vertex contract.  v_vPosition is required by the
        // Chapter 4 prophecy shaders and harmless when a fragment does not use
        // it.  Replacing only the generated vertex stage cuts compile pressure
        // sharply while retaining the game's original fragment calculations.
        static const char* compactVertexVita =
            "attribute vec3 in_Position; attribute vec4 in_Colour; attribute vec2 in_TextureCoord;\n"
            "uniform mat4 u_vitaMVP; varying vec2 v_vTexcoord; varying vec4 v_vColour; varying vec2 v_vPosition;\n"
            "void main(){ gl_Position=u_vitaMVP*vec4(in_Position,1.0); v_vColour=in_Colour; v_vTexcoord=in_TextureCoord; v_vPosition=in_Position.xy; }\n";
        static const char* shadowFragmentVita =
            "precision mediump float; uniform sampler2D gm_BaseTexture; uniform vec4 shadowCol; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 c=v_vColour*texture2D(gm_BaseTexture,v_vTexcoord); c.rgb=shadowCol.rgb; if(c.a>0.0)c.a=min(shadowCol.a,c.a); gl_FragColor=c; }\n";
        static const char* shadowEveningFragmentVita =
            "precision mediump float; uniform sampler2D gm_BaseTexture; uniform vec4 shadowCol; uniform vec2 pixelw; varying vec2 v_vTexcoord; varying vec4 v_vColour;\n"
            "void main(){ vec4 c=v_vColour*texture2D(gm_BaseTexture,v_vTexcoord); float px=v_vTexcoord.x-pixelw.x*6.0; if(px<pixelw.y||texture2D(gm_BaseTexture,vec2(px,v_vTexcoord.y)).a<=0.0){c.rgb=shadowCol.rgb;if(c.a>0.0)c.a=min(shadowCol.a,c.a);} gl_FragColor=c; }\n";
        if (strcmp(shaderName, "shd_hue") == 0) { vsrc=hueVertexVita; fsrc=hueFragmentVita; }
        if (strcmp(shaderName, "shd_pal_swapper") == 0) { vsrc=paletteVertexVita; fsrc=paletteFragmentVita; }
        if (strcmp(shaderName, "shd_shadowblend") == 0) fsrc=shadowFragmentVita;
        if (strcmp(shaderName, "shd_shadowblend_evening") == 0) fsrc=shadowEveningFragmentVita;
        if (strcmp(shaderName, "shd_hue") != 0 && strcmp(shaderName, "shd_pal_swapper") != 0)
            vsrc = compactVertexVita;
#else
        if (strcmp(shaderName, "shd_hue") == 0) { vsrc=hueVertexDesktop; fsrc=hueFragmentDesktop; }
        if (strcmp(shaderName, "shd_pal_swapper") == 0) { vsrc=paletteVertexDesktop; fsrc=paletteFragmentDesktop; }
        if (strcmp(shaderName, "shd_shadowblend") == 0) fsrc=shadowFragmentDesktop;
        if (strcmp(shaderName, "shd_shadowblend_evening") == 0) fsrc=shadowEveningFragmentDesktop;
        if (legacyCompactShaderSupported(shaderName) &&
            strcmp(shaderName, "shd_hue") != 0 && strcmp(shaderName, "shd_pal_swapper") != 0)
            vsrc = compactVertexDesktop;
#endif

        const char* compiledVsrc = vsrc;
        const char* compiledFsrc = fsrc;
#ifdef PLATFORM_VITA
        char* fixedVsrc = vitaFixShaderSource(vsrc);
        char* fixedFsrc = vitaFixShaderSource(fsrc);
        if (fixedVsrc == NULL || fixedFsrc == NULL) {
            free(fixedVsrc);
            free(fixedFsrc);
            continue;
        }
        compiledVsrc = fixedVsrc;
        compiledFsrc = fixedFsrc;
#endif

        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &compiledVsrc, NULL);
        glCompileShader(vs);
        GLint vsOk = GL_FALSE;
        glGetShaderiv(vs, GL_COMPILE_STATUS, &vsOk);
        if (vsOk != GL_TRUE) {
            char info[512] = {0};
            glGetShaderInfoLog(vs, sizeof(info) - 1, NULL, info);
            fprintf(stderr, "[SHDR] vertex failed %u (%s): %s\n", shaderIndex, sh->name ? sh->name : "?", info);
            glDeleteShader(vs);
#ifdef PLATFORM_VITA
            vitaLogExperimentalShader(gl, "fallback", shaderIndex, "vertex_compile");
            free(fixedVsrc);
            free(fixedFsrc);
#endif
            continue;
        }

        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &compiledFsrc, NULL);
        glCompileShader(fs);
        GLint fsOk = GL_FALSE;
        glGetShaderiv(fs, GL_COMPILE_STATUS, &fsOk);
        if (fsOk != GL_TRUE) {
            char info[512] = {0};
            glGetShaderInfoLog(fs, sizeof(info) - 1, NULL, info);
            fprintf(stderr, "[SHDR] fragment failed %u (%s): %s\n", shaderIndex, sh->name ? sh->name : "?", info);
            glDeleteShader(vs);
            glDeleteShader(fs);
#ifdef PLATFORM_VITA
            vitaLogExperimentalShader(gl, "fallback", shaderIndex, "fragment_compile");
            free(fixedVsrc);
            free(fixedFsrc);
#endif
            continue;
        }

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        // Map GameMaker attributes to VitaGL fixed-function streams.
        glBindAttribLocation(prog, 0, "in_Position");
        glBindAttribLocation(prog, 2, "in_Colour");
        glBindAttribLocation(prog, 1, "in_TextureCoord");
        glLinkProgram(prog);
        GLint linkOk = GL_FALSE;
        glGetProgramiv(prog, GL_LINK_STATUS, &linkOk);
        if (linkOk != GL_TRUE) {
            char info[512] = {0};
            glGetProgramInfoLog(prog, sizeof(info) - 1, NULL, info);
            fprintf(stderr, "[SHDR] link failed %u (%s): %s\n", shaderIndex, sh->name ? sh->name : "?", info);
            glDeleteProgram(prog);
            glDeleteShader(vs);
            glDeleteShader(fs);
#ifdef PLATFORM_VITA
            vitaLogExperimentalShader(gl, "fallback", shaderIndex, "link");
            free(fixedVsrc);
            free(fixedFsrc);
#endif
            continue;
        }

        gl->shaderPrograms[shaderIndex] = prog;
#ifdef PLATFORM_VITA
        compiledCount++;
        vitaLogExperimentalShader(gl, "compiled", shaderIndex, "none");
        fprintf(stderr, "[SHDR] compiled %u (%s)\n", shaderIndex,
                sh->name != nullptr ? sh->name : "?");
#endif
        glDeleteShader(vs);
        glDeleteShader(fs);
#ifdef PLATFORM_VITA
        free(fixedVsrc);
        free(fixedFsrc);
#endif
    }
    glUseProgram(0);
#ifdef PLATFORM_VITA
    if (progress) progress(totalSupported, totalSupported, user);
    return compiledCount;
#endif
}

#ifdef PLATFORM_VITA
static uint32_t glCompileShaderOnDemand(GLLegacyRenderer* gl, int32_t shaderIndex) {
    if (gl == nullptr || gl->base.dataWin == nullptr || shaderIndex < 0 ||
        (uint32_t)shaderIndex >= gl->base.dataWin->shdr.count) return 0;
    if (gl->shaderPrograms != nullptr && gl->shaderPrograms[shaderIndex] != 0) return 1;
    g_vitaShaderCompileOnly = shaderIndex;
    uint32_t compiled = glCompileAllShaders(gl, NULL, NULL);
    g_vitaShaderCompileOnly = -1;
    return compiled;
}
#endif

#ifdef PLATFORM_VITA
uint32_t GLLegacyRenderer_prepareShaders(GLLegacyRenderer* gl, VitaShaderPrepareProgress progress, void* user) {
    if (gl == nullptr || gl->base.dataWin == nullptr) return 0;
    extern int g_vitaShadersEnabled;
    extern void VitaProbe_logLine(const char* text);
    glUseProgram(0);
    gl->base.currentShader = -1;
    if (!g_vitaShadersEnabled) {
        if (progress) progress(1, 1, user);
        return 0;
    }
    // Chapters 4/5 can start with less than 8 MiB free in VitaGL RAM.
    // Compiling the whole audited set here still concentrated ShaRK/GXM
    // allocations and crashed Chapter 5 before the first room on hardware.
    // Keep Shaders ON, but compile each supported shader on its first actual use.
    if (progress) progress(1, 1, user);
    VitaProbe_logLine("SHADER_PRELOAD=deferred_on_demand ready=0");
    return 0;
}
#endif

static void glGpuSetShader(Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
#ifdef PLATFORM_VITA
    extern int g_vitaShadersEnabled;
    if (!g_vitaShadersEnabled) {
        renderer->currentShader = -1;
        glUseProgram(0);
        return;
    }
    // A few chapters query shader handles from global/Create code before the
    // visible loading pass gets a chance to prepare programs.  Compile the
    // audited set on that first request so those handles never become a
    // permanent -1 for the remainder of the chapter.
    const bool validShader = shaderIndex >= 0 &&
        (uint32_t)shaderIndex < gl->base.dataWin->shdr.count;
    const bool compactSupported = validShader &&
        legacyCompactShaderSupported(gl->base.dataWin->shdr.shaders[shaderIndex].name);
    if (validShader && GLLegacyRenderer_experimentalShadersEnabled(gl) &&
        !compactSupported && gl->shaderAttempted != NULL &&
        !gl->shaderAttempted[shaderIndex]) {
        gl->shaderAttempted[shaderIndex] = true;
        vitaLogExperimentalShader(gl, "fallback", (uint32_t)shaderIndex, "unsupported");
    }
    if (validShader &&
        gl->shaderPrograms != NULL && gl->shaderPrograms[shaderIndex] == 0 &&
        compactSupported)
        (void)glCompileShaderOnDemand(gl, shaderIndex);
#endif
    renderer->currentShader = shaderIndex;
    static const DataWin* loggedDataWin = nullptr;
    static bool loggedShaders[1024] = { false };
    static bool castlePaletteFallbackLogged = false;
    if (loggedDataWin != gl->base.dataWin) {
        memset(loggedShaders, 0, sizeof(loggedShaders));
        castlePaletteFallbackLogged = false;
        loggedDataWin = gl->base.dataWin;
    }
    if (shaderIndex >= 0 && shaderIndex < (int32_t)(sizeof(loggedShaders) / sizeof(loggedShaders[0])) &&
        !loggedShaders[shaderIndex]) {
        const char* name = "<invalid>";
        if (shaderIndex >= 0 && (uint32_t)shaderIndex < gl->base.dataWin->shdr.count &&
            gl->base.dataWin->shdr.shaders[shaderIndex].name != nullptr) {
            name = gl->base.dataWin->shdr.shaders[shaderIndex].name;
        }
#ifdef PLATFORM_VITA
        extern void VitaProbe_logLine(const char* text);
        char line[192];
        const bool adapted = shaderIndex >= 0 &&
                             (uint32_t)shaderIndex < gl->base.dataWin->shdr.count &&
                             gl->shaderPrograms != nullptr &&
                             gl->shaderPrograms[shaderIndex] != 0;
        if (GLLegacyRenderer_experimentalShadersEnabled(gl)) {
            if (adapted)
                vitaLogExperimentalShader(gl, "active", (uint32_t)shaderIndex, "none");
        } else {
            snprintf(line, sizeof(line), "SHADER_USE index=%d name=%s path=%s",
                     shaderIndex, name, adapted ? "adapted_legacy" : "fixed_fallback");
            VitaProbe_logLine(line);
        }
#else
        fprintf(stderr, "SHADER_USE index=%d name=%s\n", shaderIndex, name);
#endif
        loggedShaders[shaderIndex] = true;
    }
    if (shaderIndex < 0 || (uint32_t)shaderIndex >= gl->base.dataWin->shdr.count ||
        gl->shaderPrograms == nullptr || gl->shaderPrograms[shaderIndex] == 0) {
#ifdef PLATFORM_VITA
        vitaCustomShaderActive = false;
#endif
        glUseProgram(0);
        return;
    }

#ifdef PLATFORM_VITA
    // The compact palette shader can execute up to 256 dependent texture
    // reads per pixel. Castle Town interiors apply it over large surfaces;
    // the captured GXM dump ends in that pass after sustained 60-67 ms GPU
    // frames. Preserve the shader elsewhere (notably Chapter 3's long quiz
    // palettes), but use the stable textured fallback in these interiors.
    const char* activeShaderName = gl->base.dataWin->shdr.shaders[shaderIndex].name;
    const char* activeRoomName = gl->base.runner != nullptr &&
        gl->base.runner->currentRoom != nullptr ? gl->base.runner->currentRoom->name : nullptr;
    extern int g_vitaActiveChapter;
    if (g_vitaActiveChapter == 5 && activeShaderName != nullptr && activeRoomName != nullptr &&
        strcmp(activeShaderName, "shd_pal_swapper") == 0 &&
        (strncmp(activeRoomName, "room_dw_castle_", sizeof("room_dw_castle_") - 1) == 0 ||
         strncmp(activeRoomName, "room_dw_ralsei_castle_", sizeof("room_dw_ralsei_castle_") - 1) == 0)) {
        vitaCustomShaderActive = false;
        glUseProgram(0);
        if (!castlePaletteFallbackLogged) {
            vitaLogExperimentalShader(gl, "fallback", (uint32_t)shaderIndex,
                                      "castle_palette_gpu_budget");
            castlePaletteFallbackLogged = true;
        }
        return;
    }
#endif

    GLuint program = gl->shaderPrograms[shaderIndex];
#ifdef PLATFORM_VITA
    vitaCustomShaderActive = true;
#endif
    glUseProgram(program);

    // GameMaker shaders project every immediate-mode vertex through this
    // array. The previous Vita experiment omitted it, leaving the matrix at
    // zero and making affected sprites invisible.
    GLint matrices = glGetUniformLocation(program,
#ifdef PLATFORM_VITA
                                            "gm_Matrices"
#else
                                            "gm_Matrices[0]"
#endif
    );
    if (matrices >= 0) {
        Matrix4f shaderMatrices[MATRICES_MAX];
        memcpy(shaderMatrices, renderer->gmlMatrices, sizeof(shaderMatrices));
        Matrix4f_flipClipY(&shaderMatrices[MATRIX_PROJECTION]);
        Matrix4f_flipClipY(&shaderMatrices[MATRIX_WORLD_VIEW_PROJECTION]);
        glUniformMatrix4fv(matrices, MATRICES_MAX, GL_FALSE, shaderMatrices[0].m);
    }
    GLint vitaMVP = glGetUniformLocation(program, "u_vitaMVP");
    if (vitaMVP >= 0) {
        Matrix4f mvp = renderer->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION];
        Matrix4f_flipClipY(&mvp);
        glUniformMatrix4fv(vitaMVP, 1, GL_FALSE, mvp.m);
    }

    GLint baseTexture = glGetUniformLocation(program, "gm_BaseTexture");
    if (baseTexture >= 0) glUniform1i(baseTexture, 0);
    GLint lighting = glGetUniformLocation(program, "gm_LightingEnabled");
    if (lighting >= 0) glUniform1i(lighting, GL_FALSE);
    GLint vertexFog = glGetUniformLocation(program, "gm_VS_FogEnabled");
    if (vertexFog >= 0) glUniform1i(vertexFog, GL_FALSE);
    GLint pixelFog = glGetUniformLocation(program, "gm_PS_FogEnabled");
    if (pixelFog >= 0) glUniform1i(pixelFog, GL_FALSE);
    GLint alphaTest = glGetUniformLocation(program, "gm_AlphaTestEnabled");
    if (alphaTest >= 0) glUniform1i(alphaTest, GL_FALSE);
    GLint alphaRef = glGetUniformLocation(program, "gm_AlphaRefValue");
    if (alphaRef >= 0) glUniform1f(alphaRef, 0.0f);

    // Chapter 5's intro obtains these sampler handles through a generated
    // extension wrapper which is not executed by Butterscotch. Bind the three
    // documented shd_shoujo inputs directly by resource name. This is stable
    // across rebuilt atlases and translations because it does not use TPAG or
    // texture-page numbers.
    const char* shaderName = gl->base.dataWin->shdr.shaders[shaderIndex].name;
    if (shaderName != NULL && strcmp(shaderName, "shd_shoujo") == 0) {
#ifndef PLATFORM_VITA
        static bool loggedShoujoResources = false;
        if (!loggedShoujoResources) {
            const uint32_t ids[] = {6087U, 8382U, 7074U};
            for (uint32_t i = 0; i < 3U; ++i) {
                if (ids[i] < gl->base.dataWin->sprt.count)
                    fprintf(stderr, "SHOUJO_RESOURCE id=%u name=%s\n", ids[i],
                            gl->base.dataWin->sprt.sprites[ids[i]].name);
            }
            loggedShoujoResources = true;
        }
#endif
        static const char* samplerNames[] = { "texGradient", "texBubble", "texStars" };
        static const char* spriteNames[] = { "spr_shoujogradient", "spr_bubl", "spr_stars_2" };
        for (int stage = 1; stage <= 3; ++stage) {
            GLint sampler = glGetUniformLocation(program, samplerNames[stage - 1]);
            if (sampler >= 0) glUniform1i(sampler, stage);
            for (uint32_t s = 0; s < gl->base.dataWin->sprt.count; ++s) {
                Sprite* sprite = &gl->base.dataWin->sprt.sprites[s];
                if (!sprite->present || sprite->name == NULL ||
                    strcmp(sprite->name, spriteNames[stage - 1]) != 0 ||
                    sprite->textureCount == 0 || sprite->tpagIndices == NULL ||
                    sprite->tpagIndices[0] < 0)
                    continue;
                glTextureSetStage(renderer, stage,
                                  (uint32_t)sprite->tpagIndices[0] + 1U);
                break;
            }
        }
    }
}
static void glGpuResetShader(Renderer* renderer) {
    renderer->currentShader = -1;
#ifdef PLATFORM_VITA
    vitaCustomShaderActive = false;
#endif
    glUseProgram(0);
    // Disable extra texture stages enabled by texture_set_stage (e.g. pal_swap)
    // so fixed-function pipeline multi-texturing doesn't corrupt subsequent draws.
    for (int i = 1; i < 8; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);
}
static int32_t glShaderGetUniform(Renderer* renderer, int32_t shaderIndex, char* uniform) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
#ifdef PLATFORM_VITA
    extern int g_vitaShadersEnabled;
    if (g_vitaShadersEnabled && shaderIndex >= 0 &&
        (uint32_t)shaderIndex < gl->base.dataWin->shdr.count &&
        gl->shaderPrograms && gl->shaderPrograms[shaderIndex] == 0 &&
        legacyCompactShaderSupported(gl->base.dataWin->shdr.shaders[shaderIndex].name))
        (void)glCompileShaderOnDemand(gl, shaderIndex);
#endif
    if (shaderIndex >= 0 && gl->shaderPrograms && gl->shaderPrograms[shaderIndex])
        return glGetUniformLocation(gl->shaderPrograms[shaderIndex], uniform);
    return -1;
}
static int32_t glShaderGetSamplerIndex(Renderer* renderer, int32_t shaderIndex, char* uniform) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
#ifdef PLATFORM_VITA
    extern int g_vitaShadersEnabled;
    if (g_vitaShadersEnabled && shaderIndex >= 0 &&
        (uint32_t)shaderIndex < gl->base.dataWin->shdr.count &&
        gl->shaderPrograms && gl->shaderPrograms[shaderIndex] == 0 &&
        legacyCompactShaderSupported(gl->base.dataWin->shdr.shaders[shaderIndex].name))
        (void)glCompileShaderOnDemand(gl, shaderIndex);
#endif
    if (shaderIndex >= 0 && gl->shaderPrograms && gl->shaderPrograms[shaderIndex]) {
        // GameMaker returns a texture stage, while OpenGL returns a uniform
        // location. Auxiliary samplers must occupy distinct stages: shd_shoujo
        // uses gradient, bubble and star textures simultaneously. Binding all
        // of them to stage 1 made the Chapter 5 logo render as a white mask.
        GLuint previousProgram = renderer->currentShader >= 0 &&
                                 (uint32_t)renderer->currentShader < gl->base.dataWin->shdr.count &&
                                 gl->shaderPrograms[renderer->currentShader]
            ? gl->shaderPrograms[renderer->currentShader] : 0;
        GLuint program = gl->shaderPrograms[shaderIndex];
        glUseProgram(program);
        GLint location = glGetUniformLocation(program, uniform);
        int32_t stage = 1;
        if (strcmp(uniform, "texBubble") == 0) stage = 2;
        else if (strcmp(uniform, "texStars") == 0) stage = 3;
        else if (strcmp(uniform, "texGradient") == 0) stage = 1;
        else if (location >= 0) stage = 1 + (location % 7);
        if (location >= 0) glUniform1i(location, stage);
#ifndef PLATFORM_VITA
        fprintf(stderr, "SHADER_SAMPLER shader=%d name=%s location=%d stage=%d\n",
                shaderIndex, uniform != NULL ? uniform : "?", (int)location, (int)stage);
#endif
        if (previousProgram != program) glUseProgram(previousProgram);
        return location >= 0 ? stage : -1;
    }
    return -1;
}
static void glShaderSetUniformF(MAYBE_UNUSED Renderer* renderer, int32_t handle, int32_t count, float value1, float value2, float value3, float value4) {
    if (handle < 0) return;
    if (count == 1) glUniform1f(handle, value1);
    else if (count == 2) glUniform2f(handle, value1, value2);
    else if (count == 3) glUniform3f(handle, value1, value2, value3);
    else if (count == 4) glUniform4f(handle, value1, value2, value3, value4);
}
static void glShaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer, int32_t handle, float* values, uint32_t count) {
    if (handle >= 0 && values && count > 0) glUniform4fv(handle, count, values);
}
static void glShaderSetUniformI(MAYBE_UNUSED Renderer* renderer, int32_t handle, int32_t count, int32_t value1, int32_t value2, int32_t value3, int32_t value4) {
    if (handle < 0) return;
    if (count == 1) glUniform1i(handle, value1);
    else if (count == 2) glUniform2i(handle, value1, value2);
    else if (count == 3) glUniform3i(handle, value1, value2, value3);
    else if (count == 4) glUniform4i(handle, value1, value2, value3, value4);
}
static bool glShaderIsCompiled(Renderer* renderer, int32_t shaderIndex) {
    GLLegacyRenderer* gl = (GLLegacyRenderer*) renderer;
#ifdef PLATFORM_VITA
    extern int g_vitaShadersEnabled;
    if (g_vitaShadersEnabled && shaderIndex >= 0 &&
        (uint32_t)shaderIndex < gl->base.dataWin->shdr.count &&
        gl->shaderPrograms && gl->shaderPrograms[shaderIndex] == 0 &&
        legacyCompactShaderSupported(gl->base.dataWin->shdr.shaders[shaderIndex].name))
        (void)glCompileShaderOnDemand(gl, shaderIndex);
#endif
    if (shaderIndex >= 0 && gl->shaderPrograms && shaderIndex < (int32_t) gl->base.dataWin->shdr.count)
        return gl->shaderPrograms[shaderIndex] != 0;
    return false;
}
static bool glShadersSupported(void) {
#ifdef PLATFORM_VITA
    extern int g_vitaShadersEnabled;
    return g_vitaShadersEnabled != 0;
#else
    return true;
#endif
}

static RendererVtable glVtable;

// ===[ Public API ]===

Renderer* GLLegacyRenderer_create(void) {
    GLLegacyRenderer* gl = (GLLegacyRenderer *)safeCalloc(1, sizeof(GLLegacyRenderer));
    gl->base.vtable = &glVtable;
    glVtable.init = glInit;
    glVtable.destroy = glDestroy;
    glVtable.beginFrame = glBeginFrame;
    glVtable.endFrameInit = glEndFrameInit;
    glVtable.endFrameEnd = glEndFrameEnd;
    glVtable.beginView = glBeginView;
    glVtable.endView = glEndView;
    glVtable.applyProjection = glApplyProjection;
    glVtable.beginGUI = glBeginGUI;
    glVtable.setGuiProjection = glSetGuiProjection;
    glVtable.endGUI = glEndGUI;
    glVtable.drawSprite = glDrawSprite;
    glVtable.drawSpritePos = glDrawSpritePos;
    glVtable.drawSpritePart = glDrawSpritePart;
    glVtable.drawRectangle = glDrawRectangle;
    glVtable.drawRectangleColor = glDrawRectangleColor;
    glVtable.drawLine = glDrawLine;
    glVtable.drawLineColor = glDrawLineColor;
    glVtable.drawTriangle = glDrawTriangle;
    glVtable.drawText = glDrawText;
    glVtable.drawTextColor = glDrawTextColor;
    glVtable.flush = glRendererFlush;
    glVtable.clearScreen = glClearScreen;
    glVtable.createSpriteFromSurface = glCreateSpriteFromSurface;
    glVtable.deleteSprite = glDeleteSprite;
    glVtable.gpuGetBlendFactors = glGpuGetBlendFactors;
    glVtable.gpuGetBlendMode = glGpuGetBlendMode;
    glVtable.gpuSetBlendMode = glGpuSetBlendMode;
    glVtable.gpuSetBlendModeExt = glGpuSetBlendModeExt;
    glVtable.gpuSetBlendEnable = glGpuSetBlendEnable;
    glVtable.gpuSetAlphaTestEnable = glGpuSetAlphaTestEnable;
    glVtable.gpuSetAlphaTestRef = glGpuSetAlphaTestRef;
    glVtable.gpuSetColorWriteEnable = glGpuSetColorWriteEnable;
    glVtable.gpuGetColorWriteEnable = glGpuGetColorWriteEnable;
    glVtable.gpuGetBlendEnable = glGpuGetBlendEnable;
    glVtable.drawTile = nullptr;
    glVtable.drawSpriteTiled = glDrawSpriteTiled;
    glVtable.createSurface = glLegacyCreateSurface;
    glVtable.surfaceExists = glLegacySurfaceExists;
    glVtable.setRenderTarget = glLegacySetRenderTarget;
    glVtable.ensureApplicationSurface = glLegacyEnsureApplicationSurface;
    glVtable.getSurfaceWidth = glLegacyGetSurfaceWidth;
    glVtable.getSurfaceHeight = glLegacyGetSurfaceHeight;
    glVtable.drawSurface = glLegacyDrawSurface;
    glVtable.drawSurfaceColor = glLegacyDrawSurfaceColor;
    glVtable.drawSurfaceTiled = glLegacyDrawSurfaceTiled;
    glVtable.surfaceResize = glLegacySurfaceResize;
    glVtable.surfaceFree = glLegacySurfaceFree;
    glVtable.surfaceCopy = glLegacySurfaceCopy;
    glVtable.surfaceGetPixels = glLegacySurfaceGetPixels;
    glVtable.spriteGetTexture = glSpriteGetTexture;
    glVtable.surfaceGetTexture = glSurfaceGetTexture;
    glVtable.textureGetTexelWidth = glTextureGetTexelWidth;
    glVtable.textureGetTexelHeight = glTextureGetTexelHeight;
    glVtable.textureGetUVs = glTextureGetUVs;
    glVtable.textureSetStage = glTextureSetStage;
    glVtable.gpuSetShader = glGpuSetShader;
    glVtable.gpuResetShader = glGpuResetShader;
    glVtable.shaderGetUniform = glShaderGetUniform;
    glVtable.shaderGetSamplerIndex = glShaderGetSamplerIndex;
    glVtable.shaderSetUniformF = glShaderSetUniformF;
    glVtable.shaderSetUniformFArray = glShaderSetUniformFArray;
    glVtable.shaderSetUniformI = glShaderSetUniformI;
    glVtable.shaderIsCompiled = glShaderIsCompiled;
    glVtable.shadersSupported = glShadersSupported;
    gl->base.drawColor = 0xFFFFFF; // white (BGR)
    gl->base.drawAlpha = 1.0f;
    gl->base.drawFont = -1;
    gl->base.drawHalign = 0;
    gl->base.drawValign = 0;
    gl->base.circlePrecision = 24;
    gl->colorWriteR = true;
    gl->colorWriteG = true;
    gl->colorWriteB = true;
    gl->colorWriteA = true;

    return (Renderer*) gl;
}
