#include <psp2/kernel/processmgr.h>

/* Native GXM renderer workbench. Real callbacks are defined here and their
 * exact signatures are checked by the generated RendererVtable initializer.
 * Methods not overridden still FAIL FAST. Never silently emulate success.
 */
#include "nanovg.h"
#include "runner.h"
#include "nanovg_gxm_native.h"
extern void nvgxmSetColorWriteMask(NVGcontext *ctx,unsigned mask);
#include "image/image_decoder.h"
#include "text_utils.h"
#include "vita_border_rules.h"
#include "vita_gpu_rules.h"
#include "native_bc3_swizzle.h"
#ifdef NATIVE_GXM_VIDEO
#include "native_video_gxm.h"
#endif
#include <psp2/io/stat.h>
#include <math.h>
#include <string.h>
#include <stddef.h>
#define BS_MAX_SURFACES 48
#define BS_MAX_EDGE 2048
/* GXM render targets accept up to 4096 per side; Chapter 5 creates a
 * 2080x120 strip surface, which the atlas limit above refused every frame. */
#define BS_MAX_SURFACE_EDGE 4096
#define BS_SNAPSHOT_POOL_CAP 16
#define BS_SNAPSHOT_POOL_MAX_BYTES (5u*1024u*1024u)
typedef struct {int image,w,h;} BSGxmSnapshotEntry;
typedef struct {float x,y,u,v,alpha;uint32_t color;} BSGxmVertex;
#define BS_PI 3.14159265358979323846f
/* Last blocking renderer operation, read by the main.c stall watchdog. */
volatile const char* g_gxmRenderStage=NULL;

typedef struct {
    int live, w, h, image;
    NVGXMtexture *texture;
    NVGXMframebuffer *fb;
} BSGxmSurface;
typedef struct {
    Renderer base;
    NVGcontext *vg;
    int *images, *width, *height;
    unsigned imageCount;
    int *pageImages,*pageWidth,*pageHeight;
    size_t *pageBytes;
    uint64_t *pageLastUsed;
    unsigned pageCount,cacheFrame,cacheEvictions,bc3Loads,rgba4Loads,rgba8Loads;
    uint64_t residentBytes,peakResidentBytes,cacheBudget;
    BSGxmSurface surfaces[BS_MAX_SURFACES];
    /* Retain offscreen GXM render targets until all commands sampling their
     * images have completed. Room changes/resize can free target slots
     * during Runner_step, before NanoVG has even submitted the old frame. */
    BSGxmSurface retired[BS_MAX_SURFACES*2];
    unsigned retiredCount,retiredTotal,retiredPeak;
    /* An image deleted during a Draw event is still referenced by the GPU
     * command list. Delay recycling until a fence AFTER frame presentation.
     * Bound the recycler to 5 MiB so repeated scene changes cannot fill RAM. */
    BSGxmSnapshotEntry snapshotPool[BS_SNAPSHOT_POOL_CAP];
    BSGxmSnapshotEntry snapshotRetired[BS_SNAPSHOT_POOL_CAP*2];
    unsigned snapshotPoolCount,snapshotRetiredCount, snapshotReuses,
             snapshotAllocations, snapshotDrops;
    size_t snapshotPoolBytes;
    int activeTarget, initialized, frameIsActive;
    int logicalW, logicalH, displayW, displayH, viewActive, guiActive;
    int blendMode, alphaEnable, blendEnable;
    int fogEnabled;
    uint32_t fogColor,shaderFallbacks,fogChanges;
    int shaderLastRoom;
    uint8_t shaderRoomReported[256];
    /* Uniform handles match an asset and slot, not the current program.
     * Mirrors Experimental-GL: handles are obtained during Create, then
     * assignments persist until the next call to shader_set_uniform*. */
    float shaderValues[256][4];
    float shaderVectors[256][16];
    unsigned shaderUniformWrites;
    uint8_t alphaTestRef;
    unsigned alphaTestChanges, alphaTestRefChanges;
    int writeR, writeG, writeB, writeA;
    int primitiveType, primitiveStarted;
    /* draw_primitive_* / vertex_submit vertices (any count, colour, UV). */
    BSGxmVertex *primVerts;
    unsigned primCount, primCap;
    int primImage, primW, primH;
    unsigned long draws, missing, targetSwitches, worldDraws, snapshotCount, maskFallbacks;
    unsigned long offscreenCulls, spriteFontGlyphs;
    uint64_t tileIterationsAvoided, tiledCalls, tileIterationsExecuted;
    unsigned originalSpriteCount, originalTpagCount, dynamicSprites;
    unsigned dynamicCreateTotal;
    int decodedPage,decodedW,decodedH;
    uint8_t* decodedRgba;
    unsigned long pageDecodes,itemUploads;
    unsigned long cacheHits,cacheAbsent,cacheRejected;
    uint64_t pageLoadUs;
    int viewX,viewY,viewW,viewH,portX,portY,portW,portH;
    float viewAngle;
    int guiW,guiH,guiX,guiY,guiPortW,guiPortH;
    int borderImage,borderW,borderH,borderLastRoom;
    int borderOldImage,borderOldW,borderOldH;
    float borderTransition;
    int borderControllerCrossfade;
    float borderControllerAlpha;
    char borderPath[192];
    unsigned borderChanges,borderFailures;
} BSGxmRenderer;

static BSGxmRenderer* BR(Renderer* r) { return (BSGxmRenderer*)r; }
static float bs_clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static NVGcolor bs_col(uint32_t bgr, float alpha) {
    return nvgRGBA((uint8_t)(bgr & 255), (uint8_t)((bgr >> 8) & 255),
                   (uint8_t)((bgr >> 16) & 255),
                   (uint8_t)(bs_clamp01(alpha) * 255.0f + 0.5f));
}
static int bs_pot(int n) { int p=1; while(p<n && p<BS_MAX_EDGE) p<<=1;return p; }
static void bs_trace(const char* message) {
    if(!message)return;
    SceUID fd=sceIoOpen(BS_LOG,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_APPEND,0666);
    if(fd<0)return;
    sceIoWrite(fd,message,strlen(message));
    sceIoWrite(fd,"\n",1);
    sceIoClose(fd);
}
static int bs_alpha_uniform_visible(BSGxmRenderer* b,float alpha);
/* GML-reachable states the native path cannot reproduce yet: log each kind
 * once and skip the operation instead of aborting the game. */
static void bs_soft_unsupported(const char* what) {
    static const char* seen[32];
    static unsigned seenCount=0;
    for(unsigned i=0;i<seenCount;i++)if(seen[i]==what)return;
    if(seenCount<32)seen[seenCount++]=what;
    char line[140];
    snprintf(line,sizeof(line),"NATIVE_GXM=runner_renderer skipped=%s",what);
    bs_trace(line);
}
static int bs_valid(BSGxmRenderer* b) { return b && b->vg != NULL; }
static void bs_snapshot_sync(BSGxmRenderer* b,int cpuWait);
void BSContractRenderer_bind(Renderer* r, NVGcontext* vg) { if(r) BR(r)->vg=vg; }
static void bs_require_renderer(BSGxmRenderer* b,const char* call) {
    if (!bs_valid(b)) bs_unsupported(call);
}

#include "rgba4444_native.h"

/* All sampled game-atlas pages (including sprite fonts) use native
 * RGBA4444. Do not silently switch to RGBA8888/BC3 based on saved
 * config, language overlays, or font alpha: that reintroduces format
 * mismatches and memory spikes. Render targets are separate and retain
 * the format GXM requires for framebuffer writes. */

/* Prepared lossless pages written by the VitaGL texture preparer
 * (gl_legacy_renderer.c: <chapter root>/texture-cache/page_NNN.r444).
 * They hold GL_UNSIGNED_SHORT_4_4_4_4 (0xRGBA), which is GXM layout 0, so a
 * valid page uploads without decoding the PNG/QOI atlas. Header, size or
 * dimension mismatches fall back to data.win. */
typedef struct {
    uint32_t magic,sourceSize,sourceOffset,width,height;
} BSGxmCacheHeader;
#define BS_CACHE_MAGIC 0x31435456U /* VTC1 */
#define BS_CACHE_CH3_MAGIC 0x35435456U /* VTC5 */
static int bs_cache_chapter(const DataWin* dw) {
    const char* m=dw&&dw->lazyLoadFilePath?strstr(dw->lazyLoadFilePath,"chapter"):NULL;
    return m&&m[7]>='0'&&m[7]<='5'?m[7]-'0':0;
}
/* Same language-to-folder mapping as vitaTextureLangFolder, so both
 * backends read one cache. */
static const char* bs_cache_lang(const DataWin* dw) {
    const char* p=dw?dw->lazyLoadFilePath:NULL;
    if(!p)return NULL;
    static const char* const map[][2]={
        {"mods/Lang/Portuguese-BR/","Portuguese-BR"},{"mods/Lang/Spanish/","Spanish"},
        {"mods/Lang/Italian/","Italian"},{"mods/Lang/Turkish/","Turkish"},
        {"mods/Lang/German/","German"},{"mods/Lang/Russian/","Russkiy"},
        {"mods/Lang/Russkiy/","Russkiy"},{"mods/Lang/French/","French"}
    };
    for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++)
        if(strstr(p,map[i][0]))return map[i][1];
    return NULL;
}
static uint16_t* bs_load_cached_page(BSGxmRenderer* b,const DataWin* dw,
                                     uint32_t page,int* w,int* h) {
    /* gxm-r444-cache.txt containing 0 forces the data.win decode path, so
     * a hardware A/B test does not need another build. */
    static int cacheEnabled=-1;
    if(cacheEnabled<0){
        cacheEnabled=1;
        FILE*f=fopen("ux0:data/deltarune/deltarunevita/gxm-r444-cache.txt","rb");
        if(f){cacheEnabled=fgetc(f)!='0';fclose(f);}
        bs_trace(cacheEnabled?"NATIVE_GXM=r444_cache enabled=1":
                              "NATIVE_GXM=r444_cache enabled=0 source=gxm-r444-cache.txt");
    }
    if(!cacheEnabled)return NULL;
    const Texture* t=&dw->txtr.textures[page];
    const char* lang=bs_cache_lang(dw);
    char path[256];
    if(lang)snprintf(path,sizeof(path),
        "ux0:data/deltarune/deltarunevita/mods/Lang/%s/chapter%d/texture-cache/page_%03u.r444",
        lang,bs_cache_chapter(dw),page);
    else snprintf(path,sizeof(path),
        "ux0:data/deltarune/deltarunevita/chapter%d/texture-cache/page_%03u.r444",
        bs_cache_chapter(dw),page);
    SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0){b->cacheAbsent++;return NULL;}
    BSGxmCacheHeader hd;
    uint32_t magic=bs_cache_chapter(dw)==3?BS_CACHE_CH3_MAGIC:BS_CACHE_MAGIC;
    int got=sceIoRead(fd,&hd,sizeof(hd));
    SceOff end=sceIoLseek(fd,0,SCE_SEEK_END);
    size_t bytes=(size_t)hd.width*(size_t)hd.height*sizeof(uint16_t);
    if(got!=(int)sizeof(hd)||hd.magic!=magic||
       hd.sourceSize!=t->blobSize||hd.sourceOffset!=t->blobOffset||
       hd.width==0||hd.height==0||hd.width>BS_MAX_EDGE||hd.height>BS_MAX_EDGE||
       (t->textureWidth>0&&hd.width!=(uint32_t)t->textureWidth)||
       (t->textureHeight>0&&hd.height!=(uint32_t)t->textureHeight)||
       end!=(SceOff)(sizeof(hd)+bytes)||
       sceIoLseek(fd,sizeof(hd),SCE_SEEK_SET)!=(SceOff)sizeof(hd)){
        sceIoClose(fd);b->cacheRejected++;return NULL;
    }
    uint16_t* packed=(uint16_t*)malloc(bytes);
    if(!packed||sceIoRead(fd,packed,(unsigned)bytes)!=(int)bytes){
        free(packed);sceIoClose(fd);b->cacheRejected++;return NULL;
    }
    sceIoClose(fd);
    int layout=gxmRGBA4444Layout();
    if(layout!=0){
        for(size_t i=0;i<bytes/2;i++){
            uint16_t v=packed[i];
            packed[i]=gxmRGBA4444PackVariant((uint8_t)((v>>12&15)<<4),
                (uint8_t)((v>>8&15)<<4),(uint8_t)((v>>4&15)<<4),
                (uint8_t)((v&15)<<4),layout);
        }
    }
    *w=(int)hd.width;*h=(int)hd.height;
    return packed;
}

/* ---- texture-cache generator (same files as gl_legacy_renderer.c) ---- */
#define BS_CACHE_COMPLETE_MAGIC 0x32435456U     /* VTC2 */
#define BS_CACHE_CH3_COMPLETE_MAGIC 0x36435456U /* VTC6 */
static void bs_cache_dir(const DataWin* dw,char* out,size_t cap){
    const char* lang=bs_cache_lang(dw);
    if(lang)snprintf(out,cap,"ux0:data/deltarune/deltarunevita/mods/Lang/%s/chapter%d/texture-cache",
                     lang,bs_cache_chapter(dw));
    else snprintf(out,cap,"ux0:data/deltarune/deltarunevita/chapter%d/texture-cache",
                  bs_cache_chapter(dw));
}
static int bs_cache_page_valid(const DataWin* dw,uint32_t page,const char* dir){
    const Texture* t=&dw->txtr.textures[page];
    char path[300];
    snprintf(path,sizeof(path),"%s/page_%03u.r444",dir,page);
    SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0)return 0;
    BSGxmCacheHeader hd;
    int got=sceIoRead(fd,&hd,sizeof(hd));
    SceOff end=sceIoLseek(fd,0,SCE_SEEK_END);
    sceIoClose(fd);
    uint32_t magic=bs_cache_chapter(dw)==3?BS_CACHE_CH3_MAGIC:BS_CACHE_MAGIC;
    return got==(int)sizeof(hd)&&hd.magic==magic&&hd.sourceSize==t->blobSize&&
           hd.sourceOffset==t->blobOffset&&hd.width>0&&hd.height>0&&
           end==(SceOff)(sizeof(hd)+(size_t)hd.width*hd.height*2u);
}
static int bs_cache_marked_complete(const DataWin* dw,const char* dir){
    char path[300];
    snprintf(path,sizeof(path),"%s/complete.vtc",dir);
    SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0)return 0;
    uint32_t marker[2]={0,0};
    int got=sceIoRead(fd,marker,sizeof(marker));
    sceIoClose(fd);
    uint32_t magic=bs_cache_chapter(dw)==3?BS_CACHE_CH3_COMPLETE_MAGIC:BS_CACHE_COMPLETE_MAGIC;
    return got==(int)sizeof(marker)&&marker[0]==magic&&marker[1]==dw->txtr.count;
}
unsigned BSContractRenderer_textureCacheMissing(Renderer* r){
    DataWin* dw=r?r->dataWin:NULL;
    if(!dw||!dw->txtr.count)return 0;
    char dir[240];
    bs_cache_dir(dw,dir,sizeof(dir));
    if(bs_cache_marked_complete(dw,dir))return 0;
    unsigned missing=0;
    for(uint32_t i=0;i<dw->txtr.count;i++){
        const Texture* t=&dw->txtr.textures[i];
        if(t->blobOffset==0||t->blobSize==0)continue;
        if(!bs_cache_page_valid(dw,i,dir))missing++;
    }
    return missing;
}
unsigned BSContractRenderer_generateTextureCache(Renderer* r,
    void (*progress)(unsigned done,unsigned total,void* user),void* user){
    DataWin* dw=r?r->dataWin:NULL;
    if(!dw||!dw->txtr.count)return 0;
    char dir[240];
    bs_cache_dir(dw,dir,sizeof(dir));
    sceIoMkdir(dir,0777);
    uint32_t magic=bs_cache_chapter(dw)==3?BS_CACHE_CH3_MAGIC:BS_CACHE_MAGIC;
    bool gm2022_5=DataWin_isVersionAtLeast(dw,2022,5,0,0);
    unsigned written=0,failed=0;
    for(uint32_t page=0;page<dw->txtr.count;page++){
        Texture* t=&dw->txtr.textures[page];
        if(t->blobOffset!=0&&t->blobSize!=0&&!bs_cache_page_valid(dw,page,dir)){
            DataWin_loadTxtrIfNeeded(dw,page);
            int w=0,h=0;
            uint8_t* rgba=t->blobData?ImageDecoder_decodeToRgba(t->blobData,t->blobSize,
                                                               gm2022_5,&w,&h):NULL;
            if(!t->mapped){free(t->blobData);t->blobData=NULL;}
            if(rgba&&w>0&&h>0&&w<=4096&&h<=4096){
                size_t count=(size_t)w*(size_t)h;
                uint16_t* packed=(uint16_t*)rgba; /* in place: 2 bytes behind 4 */
                for(size_t i=0;i<count;i++){
                    const uint8_t* src=&rgba[i*4];
                    uint16_t a4=src[3]==0?0:(uint16_t)((src[3]+15u)>>4);
                    if(a4>15u)a4=15u;
                    packed[i]=(uint16_t)(((uint16_t)(src[0]>>4)<<12)|
                                         ((uint16_t)(src[1]>>4)<<8)|
                                         ((uint16_t)(src[2]>>4)<<4)|a4);
                }
                char path[300];
                snprintf(path,sizeof(path),"%s/page_%03u.r444",dir,page);
                SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
                if(fd>=0){
                    BSGxmCacheHeader hd={magic,t->blobSize,t->blobOffset,(uint32_t)w,(uint32_t)h};
                    int ok=sceIoWrite(fd,&hd,sizeof(hd))==(int)sizeof(hd)&&
                           sceIoWrite(fd,packed,(SceSize)(count*2u))==(int)(count*2u);
                    sceIoClose(fd);
                    if(ok)written++;else{sceIoRemove(path);failed++;}
                }else failed++;
            }else failed++;
            free(rgba);
        }
        if(progress)progress(page+1,dw->txtr.count,user);
    }
    if(!failed){
        char path[300];
        snprintf(path,sizeof(path),"%s/complete.vtc",dir);
        SceUID fd=sceIoOpen(path,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
        if(fd>=0){
            uint32_t marker[2]={bs_cache_chapter(dw)==3?BS_CACHE_CH3_COMPLETE_MAGIC:
                                BS_CACHE_COMPLETE_MAGIC,dw->txtr.count};
            sceIoWrite(fd,marker,sizeof(marker));
            sceIoClose(fd);
        }
    }
    char line[140];
    snprintf(line,sizeof(line),"NATIVE_GXM=texture_cache_generated written=%u failed=%u pages=%u",
             written,failed,dw->txtr.count);
    bs_trace(line);
    return written;
}
static int bs_upload_page(BSGxmRenderer* b,int tpag) {
    DataWin*dw=b->base.dataWin;
    if(!dw||tpag<0||(unsigned)tpag>=b->originalTpagCount)return 0;
    TexturePageItem*tp=&dw->tpag.items[tpag];
    int page=tp->texturePageId;
    if(!tp->present||page<0||(unsigned)page>=b->pageCount)return 0;
    if(b->pageImages[page]>0){
       b->pageLastUsed[page]=b->cacheFrame;
       b->width[tpag]=b->pageWidth[page];
       b->height[tpag]=b->pageHeight[page];
       return b->pageImages[page];
    }
    int w=0,h=0,image=0;
    size_t size=0;
    uint64_t loadStartUs=sceKernelGetProcessTimeWide();
    g_gxmRenderStage="texture_cache_read";
    uint16_t*cached=bs_load_cached_page(b,dw,(uint32_t)page,&w,&h);
    uint8_t*rgba=NULL;
    if(!cached){
        g_gxmRenderStage="texture_decode";
        DataWin_loadTxtrIfNeeded(dw,(uint32_t)page);
        Texture*src=&dw->txtr.textures[page];
        if(!src->blobData||!src->blobSize){g_gxmRenderStage=NULL;return 0;}
        rgba=ImageDecoder_decodeToRgba(src->blobData,src->blobSize,
            DataWin_isVersionAtLeast(dw,2022,5,0,0),&w,&h);
        /* Match the GL renderers: the compressed blob is re-read from
         * data.win after an eviction instead of staying in RAM for the
         * whole chapter. */
        if(!src->mapped){free(src->blobData);src->blobData=NULL;}
        if(!rgba||w<=0||h<=0||w>BS_MAX_EDGE||h>BS_MAX_EDGE){
            free(rgba);g_gxmRenderStage=NULL;return 0;
        }
    }
    /* Print the layout on the FIRST uploaded page: Chapter0 can draw its
     * entire opening using texture pages other than page 0. */
    static int firstLayoutLogged=0;
    if(!firstLayoutLogged++){
        char status[150];
        snprintf(status,sizeof(status),
             "NATIVE_GXM=rgba4444_channel_layout variant=%d first_page=%d",
             gxmRGBA4444Layout(),page);
        bs_trace(status);
    }
    g_gxmRenderStage="texture_upload";
    const int fromCache=cached!=NULL;
    if(cached){
        image=nvgCreateImageRGBA(b->vg,w,h,
            NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR|NVG_IMAGE_RGBA4444,
            (const uint8_t*)cached);
        free(cached);
        if(image>0)b->cacheHits++;
    }else{
        image=gxmRGBA4444Upload(b->vg,w,h,NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR,rgba);
        free(rgba);
    }
    g_gxmRenderStage=NULL;
    b->pageLoadUs+=sceKernelGetProcessTimeWide()-loadStartUs;
    if(image<=0){
        bs_trace("NATIVE_GXM=texture_rgba4444_upload_failed no_rgba8888_fallback");
        return 0;
    }
    size=(size_t)w*(size_t)h*2;
    b->rgba4Loads++;
    if(!fromCache)b->pageDecodes++;
    if(page==0){
        char line[150];snprintf(line,sizeof(line),
           "NATIVE_GXM=texture_format rgba4444_native layout=%d "
           "image_format=U4U4U4U4_RGBA",
           gxmRGBA4444Layout());
        bs_trace(line);
        bs_trace("NATIVE_GXM=texture_format enforced_rgba4444_all_atlases");
    }
    b->pageImages[page]=image;b->pageWidth[page]=w;b->pageHeight[page]=h;
    b->pageBytes[page]=size;b->pageLastUsed[page]=b->cacheFrame;
    b->residentBytes+=size;
    if(b->residentBytes>b->peakResidentBytes)b->peakResidentBytes=b->residentBytes;
    b->itemUploads++;
    b->width[tpag]=w;b->height[tpag]=h;
    return image;
}
static int bs_ensure_image(BSGxmRenderer*b,int tpag) {
    if(!bs_valid(b)||!b->base.dataWin||tpag<0||(unsigned)tpag>=b->imageCount)return 0;
    if((unsigned)tpag>=b->originalTpagCount)return b->images[tpag];
    return bs_upload_page(b,tpag);
}
static unsigned bs_prepare_sprite_pages(BSGxmRenderer*b,const char*name) {
    DataWin*dw=b->base.dataWin;
    unsigned loaded=0;
    for(uint32_t i=0;i<dw->sprt.count;i++){
        const Sprite*spr=&dw->sprt.sprites[i];
        if(!spr->present||!spr->name||strcmp(spr->name,name)!=0)continue;
        for(uint32_t f=0;f<spr->textureCount;f++){
            int32_t tpag=spr->tpagIndices?spr->tpagIndices[f]:-1;
            if(tpag<0||(uint32_t)tpag>=b->originalTpagCount)continue;
            int page=dw->tpag.items[tpag].texturePageId;
            int wasLoaded=page>=0&&(unsigned)page<b->pageCount&&b->pageImages[page]>0;
            if(bs_ensure_image(b,tpag)>0&&!wasLoaded)loaded++;
        }
        break;
    }
    return loaded;
}
/* Defined in native_borders.inc. */
static int bs_border_global(Runner* runner,const char* name,int def);
static Instance* bs_border_first_object(Runner* runner,const char* name);
/* Called once after a room change, before the new room's first Step.
 * Mirrors Legacy-GL's CH3_TENNA_INTRO preparation: load only the party/UI
 * pages the battle draws on its first frames, never the whole boss. */
void BSContractRenderer_roomEntered(Renderer*r) {
    BSGxmRenderer*b=BR(r);
    if(!bs_valid(b)||!b->base.dataWin||!r->runner||!r->runner->currentRoom)return;
    extern int g_vitaActiveChapter;
    const char*room=r->runner->currentRoom->name;
    if(g_vitaActiveChapter==5&&r->runner->vmContext&&
       r->runner->vmContext->globalScopeInstance){
        /* Chapter 5 defines global.opti_mode only in obj_parallax_cliffs; a
         * save loaded past the cliffs left it undefined and
         * obj_flowery_towery then skipped its final draw (invisible tower).
         * Seed the value the cliffs room assigns. runner.c does the same for
         * the VitaGL build, whose Vita runner code GXM does not compile. */
        int32_t optiId=shget(r->runner->vmContext->varNameMap,"opti_mode");
        if(optiId>=0&&Instance_getSelfVar(r->runner->vmContext->globalScopeInstance,
                                          optiId).type==RVALUE_UNDEFINED)
            Instance_setSelfVar(r->runner->vmContext->globalScopeInstance,optiId,
                                RValue_makeReal(1));
    }
    if(g_vitaActiveChapter==5&&room&&strncmp(room,"room_dw_fcastle",15)==0){
        /* The Flowey tower draws nothing when global.opti_mode is 3 and
         * skips its shaders below 1; record the state the room starts in. */
        char line[160];
        snprintf(line,sizeof(line),
          "NATIVE_GXM=ch5_room_state room=%.60s opti_mode=%d tower=%d",
          room,bs_border_global(r->runner,"opti_mode",-99),
          bs_border_first_object(r->runner,"obj_flowery_towery")!=NULL);
        bs_trace(line);
    }
    if(g_vitaActiveChapter!=3||!room||strcmp(room,"room_dw_snow_zone_battle")!=0)return;
    static const char* const intro[]={
        "spr_krisb_attackready","spr_susieb_attackready","spr_ralseib_attackready",
        "spr_krisb_idle","spr_susieb_idle","spr_ralseib_idle",
        "spr_susieb_idle_unarmed","spr_battlemsg","spr_battleblcon",
        "spr_tensionbar","spr_btact","spr_btfight"
    };
    uint64_t start=sceKernelGetProcessTimeWide();
    unsigned loaded=0;
    for(unsigned i=0;i<sizeof(intro)/sizeof(intro[0]);i++)
        loaded+=bs_prepare_sprite_pages(b,intro[i]);
    char line[140];
    snprintf(line,sizeof(line),"NATIVE_GXM=ch3_tenna_intro room_prewarm loaded=%u elapsed_ms=%llu",
             loaded,(unsigned long long)((sceKernelGetProcessTimeWide()-start)/1000u));
    bs_trace(line);
}
void BSContractRenderer_getDevMetrics(Renderer*r,
                            uint64_t*residentBytes,uint32_t*evictions) {
    BSGxmRenderer*b=BR(r);
    if(residentBytes)*residentBytes=b?(uint64_t)b->residentBytes:0;
    if(evictions)*evictions=b?(uint32_t)b->cacheEvictions:0;
}
void BSContractRenderer_cacheMaintenance(Renderer*r) {
    BSGxmRenderer*b=BR(r);
    if(!b||!b->pageImages)return;
    const char*room=(r->runner&&r->runner->currentRoom)?
                     r->runner->currentRoom->name:NULL;
    extern int g_vitaActiveChapter;
    int flowery=room&&strstr(room,"flowery")!=NULL;
    b->cacheBudget=VitaGpuRules_modernTextureCacheLimitBytes(
        g_vitaActiveChapter,room,flowery);
    if(b->residentBytes<=b->cacheBudget)return;
    int synchronized=0;
    while(b->residentBytes>b->cacheBudget){
        int victim=-1;uint64_t oldest=UINT64_MAX;
        for(unsigned i=0;i<b->pageCount;i++){
            if(b->pageImages[i]<=0 ||
               VitaGpuRules_textureRecentlyUsed(b->cacheFrame,(uint32_t)b->pageLastUsed[i]))
               continue;
            if(b->pageLastUsed[i]<oldest){victim=(int)i;oldest=b->pageLastUsed[i];}
        }
        if(victim<0)break;
        if(!synchronized){
            {g_gxmRenderStage="gpu_finish";sceGxmFinish(gxmGetWindow()->context);g_gxmRenderStage=NULL;}
            synchronized=1;
        }
        /* Fenced above and called after present: nothing in flight or
         * queued still samples this page, so reclaim it now. */
        nvgxmDeleteImageFenced(b->vg,b->pageImages[victim]);
        b->pageImages[victim]=0;
        b->residentBytes-=b->pageBytes[victim];b->pageBytes[victim]=0;
        b->cacheEvictions++;
    }
}
/* Vita hardware safety when DEV Navigator jumps between unrelated rooms:
 * the old room's texture atlas and new room's large offscreen reflections
 * otherwise coexist briefly, risking GPU/CDRAM exhaustion during swap.
 * Call only after a completed scene and GPU fence; no content is discarded
 * from DataWin and all required pages are re-decoded losslessly on demand.
 * Ordinary room changes and shader parameters are untouched. */
void BSContractRenderer_framePresented(Renderer*r) {
    BSGxmRenderer*b=BR(r);
    if(b){b->frameIsActive=0;b->activeTarget=-1;}
}
void BSContractRenderer_devRoomJumpPrepare(Renderer*r) {
    BSGxmRenderer*b=BR(r);
    if(!b||!b->vg||!b->pageImages)return;
    if(b->frameIsActive){
        bs_trace("NATIVE_GXM=dev_transition_purge rejected active_gxm_frame");
        return;
    }
    uint64_t before=(uint64_t)b->residentBytes;
    unsigned removed=0;
    for(unsigned i=0;i<b->pageCount;i++){
        if(b->pageImages[i]<=0)continue;
        /* Caller fenced the GPU after present (main.c dev_room_jump). */
        nvgxmDeleteImageFenced(b->vg,b->pageImages[i]);
        b->pageImages[i]=0;
        if(b->residentBytes>=b->pageBytes[i])
            b->residentBytes-=b->pageBytes[i];
        else b->residentBytes=0;
        b->pageBytes[i]=0;
        b->pageLastUsed[i]=0;
        b->cacheEvictions++;
        ++removed;
    }
    /* Surfaces belong to GameMaker and are freed by their own room change
     * events, not here. Prevent a deleted sprite atlas from lingering in
     * fragment sampler stages across the DEV jump. */
    nvgxmSelectRuntimeShader(b->vg,0);
    for(int stage=1;stage<=3;stage++)
        nvgxmSetRuntimeSamplerImage(b->vg,stage,0);
    char line[170];
    snprintf(line,sizeof(line),
       "NATIVE_GXM=dev_transition_purge pages=%u bytes_before=%llu bytes_after=%llu",
       removed,(unsigned long long)before,
       (unsigned long long)b->residentBytes);
    bs_trace(line);
}
/* Conservative per-image visibility test. The rectangle is converted using
 * the current NanoVG camera/model transform, so it works for views, rotated
 * sprites, surfaces and GUI. Only drop rectangles wholly outside the REAL
 * destination render target. No blind room-coordinate/world culling.
 * NaN/Inf or invalid dimensions are never classified as invisible. */
static int bs_image_offscreen(BSGxmRenderer* b,
                               float x,float y,float w,float h){
    if(!b||!b->vg||!b->frameIsActive)return 0;
    float t[6];nvgCurrentTransform(b->vg,t);
    for(unsigned k=0;k<6;k++)if(!isfinite(t[k]))return 0;
    /* Exact affine AABB: min/max independent transformed edge vectors.
     * Equivalent to all four corners, including rotation/shear/mirroring. */
    const float x0=t[0]*x+t[2]*y+t[4];
    const float y0=t[1]*x+t[3]*y+t[5];
    const float ax=t[0]*w,bx=t[2]*h,ay=t[1]*w,by=t[3]*h;
    const float xmin=x0+fminf(0.0f,ax)+fminf(0.0f,bx);
    const float xmax=x0+fmaxf(0.0f,ax)+fmaxf(0.0f,bx);
    const float ymin=y0+fminf(0.0f,ay)+fminf(0.0f,by);
    const float ymax=y0+fmaxf(0.0f,ay)+fmaxf(0.0f,by);
    if(!isfinite(xmin)||!isfinite(xmax)||!isfinite(ymin)||!isfinite(ymax))
        return 0;
    int tw=DISPLAY_WIDTH,th=DISPLAY_HEIGHT;
    if(b->activeTarget>=0&&b->activeTarget<BS_MAX_SURFACES &&
       b->surfaces[b->activeTarget].live){
        tw=b->surfaces[b->activeTarget].w;
        th=b->surfaces[b->activeTarget].h;
    }
    if(xmax < -2.0f||ymax < -2.0f ||
       xmin > (float)tw+2.0f||ymin > (float)th+2.0f){
        b->offscreenCulls++;
        return 1;
    }
    return 0;
}
/* Transform the full destination viewport back to draw-local coordinates.
 * Restricting tile loop indices to this conservative inverse AABB saves
 * entire offscreen iterations without changing texture phase/UV or draw
 * ordering. If inversion is unavailable we leave the original loop intact.
 */
typedef struct {float minX,minY,maxX,maxY;int valid;} BSGxmVisible;
static BSGxmVisible bs_visible_local(BSGxmRenderer*b) {
    BSGxmVisible r={0};
    if(!b||!b->vg||!b->frameIsActive)return r;
    float t[6],inv[6];nvgCurrentTransform(b->vg,t);
    for(int i=0;i<6;i++)if(!isfinite(t[i]))return r;
    if(!nvgTransformInverse(inv,t))return r;
    int tw=DISPLAY_WIDTH,th=DISPLAY_HEIGHT;
    if(b->activeTarget>=0&&b->activeTarget<BS_MAX_SURFACES&&
       b->surfaces[b->activeTarget].live){
       tw=b->surfaces[b->activeTarget].w;
       th=b->surfaces[b->activeTarget].h;
    }
    const float margin=3.0f;
    float x[4],y[4];
    nvgTransformPoint(&x[0],&y[0],inv,-margin,-margin);
    nvgTransformPoint(&x[1],&y[1],inv,tw+margin,-margin);
    nvgTransformPoint(&x[2],&y[2],inv,tw+margin,th+margin);
    nvgTransformPoint(&x[3],&y[3],inv,-margin,th+margin);
    r.minX=r.maxX=x[0];r.minY=r.maxY=y[0];
    for(int i=1;i<4;i++){
        if(x[i]<r.minX)r.minX=x[i];
        if(x[i]>r.maxX)r.maxX=x[i];
        if(y[i]<r.minY)r.minY=y[i];
        if(y[i]>r.maxY)r.maxY=y[i];
    }
    r.valid=isfinite(r.minX)&&isfinite(r.minY)&&
            isfinite(r.maxX)&&isfinite(r.maxY);
    return r;
}
static void bs_tile_index_span(float origin,float step,float limit,
        float visibleMin,float visibleMax,int*start,int*end){
    /* The original loop emits origin + k*step while < limit.
     * Visibility bounds are padded by a tile to keep edge fragments.
     * All calculations use double to avoid overflow/rounding near bounds.
     */
    if(!isfinite(origin)||!isfinite(step)||!isfinite(limit)||
       step<=0.0f||limit<=origin){*start=*end=0;return;}
    double total=ceil(((double)limit-origin)/step);
    if(total>=2147480000.0){*start=0;*end=0;return;}
    int n=(int)total;
    if(!isfinite(visibleMin)||!isfinite(visibleMax)){
        *start=0;*end=n;return;
    }
    double first=floor(((double)visibleMin-origin)/step)-2.0;
    double last=ceil(((double)visibleMax-origin)/step)+2.0;
    *start=(int)fmax(0.0,fmin((double)n,first));
    *end=(int)fmax((double)*start,fmin((double)n,last));
}
static void bs_image_rect(BSGxmRenderer* b,int image,int iw,int ih,
        float u,float v,float sw,float sh,
        float x,float y,float dw,float dh,float alpha) {
    if (!image || iw<=0 || ih<=0 || sw<=0 || sh<=0 ||
        dw==0 || dh==0 || alpha<=0 || !bs_alpha_uniform_visible(b,alpha)) return;
    if(bs_image_offscreen(b,x,y,dw,dh))return;
    NVGpaint paint=nvgImagePattern(b->vg,x-u*(dw/sw),y-v*(dh/sh),
                    iw*(dw/sw),ih*(dh/sh),0,image,bs_clamp01(alpha));
    nvgBeginPath(b->vg);nvgRect(b->vg,x,y,dw,dh);
    nvgFillPaint(b->vg,paint);nvgFill(b->vg);
    b->draws++;
}

static void bs_image_tinted_impl(BSGxmRenderer* b,int image,int iw,int ih,
        float u,float v,float sw,float sh,float x,float y,float dw,float dh,
        uint32_t tint,float alpha,int visibleAlready) {
    if(!image||sw<=0||sh<=0||dw==0||dh==0||alpha<=0 ||
       !bs_alpha_uniform_visible(b,alpha))return;
    if(!visibleAlready && bs_image_offscreen(b,x,y,dw,dh))return;
    NVGpaint paint=nvgImagePattern(b->vg,x-u*(dw/sw),y-v*(dh/sh),
                    iw*(dw/sw),ih*(dh/sh),0,image,1.0f);
    paint.innerColor=paint.outerColor=bs_col(tint,alpha);
    nvgBeginPath(b->vg);nvgRect(b->vg,x,y,dw,dh);
    nvgFillPaint(b->vg,paint);nvgFill(b->vg);b->draws++;
}
static void bs_image_tinted(BSGxmRenderer* b,int image,int iw,int ih,
        float u,float v,float sw,float sh,float x,float y,float dw,float dh,
        uint32_t tint,float alpha) {
    bs_image_tinted_impl(b,image,iw,ih,u,v,sw,sh,x,y,dw,dh,tint,alpha,0);
}

static void bs_tpag_region(BSGxmRenderer* b,int idx,
     float sx,float sy,float sw,float sh,float x,float y,
     float dw,float dh,float angle,float pivotX,float pivotY,float alpha,uint32_t tint) {
    /* In unrotated world/GUI draws, reject fully off-camera sprites BEFORE
     * decoding their TXTR atlas. This avoids hidden cache thrash in wide rooms.
     * Rotated sprites retain the conservative post-transform check. */
    if(angle==0.0f && bs_image_offscreen(b,x,y,dw,dh))return;
    int image=bs_ensure_image(b,idx);
    if (!image) { b->missing++;return; }
    if(angle!=0){
        nvgSave(b->vg);
        nvgTranslate(b->vg,pivotX,pivotY);
        nvgRotate(b->vg,-angle*BS_PI/180.0f);
        nvgTranslate(b->vg,-pivotX,-pivotY);
    }
    if((unsigned)idx<b->originalTpagCount){
        TexturePageItem* item=&b->base.dataWin->tpag.items[idx];
        sx+=(float)item->sourceX;sy+=(float)item->sourceY;
    }
    /* Already visibility-tested before TXTR load in unrotated case. */
    bs_image_tinted_impl(b,image,b->width[idx],b->height[idx],
          sx,sy,sw,sh,x,y,dw,dh,tint,alpha,angle==0.0f);
    if(angle!=0)nvgRestore(b->vg);
}
static int bs_surface_valid(BSGxmRenderer* b,int id) {
    return id>=0 && id<BS_MAX_SURFACES && b->surfaces[id].live;
}
static BSGxmSurface* bs_surface(BSGxmRenderer* b,int id) {
    return bs_surface_valid(b,id) ? &b->surfaces[id]:NULL;
}
static void bs_release_surface(BSGxmRenderer*b,BSGxmSurface*s){
    if(!s||!s->live)return;
    if(s->image>0&&b->vg)nvgDeleteImage(b->vg,s->image);
    if(s->fb)gxmDeleteFramebuffer(s->fb);
    if(s->texture)gxmDeleteTexture(s->texture);
    memset(s,0,sizeof(*s));
}
static void bs_release_retired(BSGxmRenderer*b){
    unsigned n=b->retiredCount;
    for(unsigned i=0;i<n;i++)bs_release_surface(b,&b->retired[i]);
    b->retiredCount=0;
}
static void bs_snapshot_pool_collect(BSGxmRenderer*b){
    for(unsigned i=0;i<b->snapshotRetiredCount;i++){
        BSGxmSnapshotEntry e=b->snapshotRetired[i];
        size_t bytes=(size_t)e.w*e.h*4u;
        if(b->snapshotPoolCount<BS_SNAPSHOT_POOL_CAP &&
           bytes<=BS_SNAPSHOT_POOL_MAX_BYTES-b->snapshotPoolBytes){
            b->snapshotPool[b->snapshotPoolCount++]=e;
            b->snapshotPoolBytes+=bytes;
        }else{
            if(e.image>0)nvgDeleteImage(b->vg,e.image);
            b->snapshotDrops++;
        }
    }
    b->snapshotRetiredCount=0;
}
static int bs_snapshot_pool_take(BSGxmRenderer*b,int w,int h){
    for(unsigned i=0;i<b->snapshotPoolCount;i++){
        BSGxmSnapshotEntry e=b->snapshotPool[i];
        if(e.w==w&&e.h==h){
            b->snapshotPool[i]=b->snapshotPool[--b->snapshotPoolCount];
            b->snapshotPoolBytes-=(size_t)w*h*4u;
            b->snapshotReuses++;
            return e.image;
        }
    }
    b->snapshotAllocations++;
    return 0;
}
static void bs_snapshot_pool_retire(BSGxmRenderer*b,int img,int w,int h){
    if(img<=0)return;
    if(!b->frameIsActive){
        /* Outside a frame we cannot assume a future presentation fence.
         * The renderer destruction path already fences before reclaiming. */
        nvgDeleteImage(b->vg,img);
    }else if(b->snapshotRetiredCount<BS_SNAPSHOT_POOL_CAP*2){
        b->snapshotRetired[b->snapshotRetiredCount++]=(BSGxmSnapshotEntry){img,w,h};
    }else{
        /* Full: force a GPU fence before deleting a potentially bound atlas. */
        bs_snapshot_sync(b,1);
        bs_snapshot_pool_collect(b);
        b->snapshotRetired[b->snapshotRetiredCount++]=(BSGxmSnapshotEntry){img,w,h};
    }
}
void BSContractRenderer_collectRetired(Renderer*r){
    BSGxmRenderer*b=BR(r);
    if(!b||(!b->retiredCount&&!b->snapshotRetiredCount))return;
    unsigned n=b->retiredCount;
    /* After nvgEndFrame/gxmEndFrame and display queue submit, ensure no
     * in-flight draw can still reference a retired sampler or framebuffer. */
    NVGXMwindow*win=gxmGetWindow();
    if(win){g_gxmRenderStage="gpu_finish";sceGxmFinish(win->context);g_gxmRenderStage=NULL;}
    bs_release_retired(b);
    bs_snapshot_pool_collect(b);
    if(n){
        char msg[120];snprintf(msg,sizeof(msg),
          "NATIVE_GXM=surface_retirement released=%u total=%u",n,b->retiredTotal);
        bs_trace(msg);
    }
}
static void bs_destroy_surface(BSGxmRenderer* b,int id) {
    BSGxmSurface* s=bs_surface(b,id);
    if(!s)return;
    if(b->frameIsActive){
        if(b->retiredCount>=BS_MAX_SURFACES*2){
            /* Pathological GML allocate/free loop: flush and synchronize
             * before reclaiming. Never overwrite unretired GPU memory. */
            bs_snapshot_sync(b,1);
            bs_release_retired(b);
        }
        b->retired[b->retiredCount++]=*s;
        b->retiredTotal++;
        if(b->retiredCount>b->retiredPeak)b->retiredPeak=b->retiredCount;
        memset(s,0,sizeof(*s));
    }else bs_release_surface(b,s);
}
static int bs_create_surface_slot(BSGxmRenderer* b,int id,int w,int h) {
    if (id<0 || id>=BS_MAX_SURFACES || w<1 || h<1 ||
        w>BS_MAX_SURFACE_EDGE || h>BS_MAX_SURFACE_EDGE) return 0;
    BSGxmSurface *s=&b->surfaces[id];
    NVGXMtexture *t=gxmCreateTexture(w,h,SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,NULL);
    if (!t) return 0;
    NVGXMframebufferInitOptions o={0};
    o.display_buffer_count=1;
    /* Original =1 was insufficient for the app surface reused across the
     * multi-pass Castle Town scene. A few native GXM passes can legitimately
     * target the same surface within a frame. */
    o.scenesPerFrame=id==0?4:2;o.render_target=t;
    o.color_format=SCE_GXM_COLOR_FORMAT_A8B8G8R8;
    o.color_surface_type=SCE_GXM_COLOR_SURFACE_LINEAR;
    o.display_width=w;o.display_height=h;o.display_stride=(w+7)&~7;
    NVGXMframebuffer *fb=gxmCreateFramebuffer(&o);
    if (!fb){gxmDeleteTexture(t);return 0;}
    int img=nvgxmCreateImageFromHandle(b->vg,&t->tex);
    if (img<=0){gxmDeleteFramebuffer(fb);gxmDeleteTexture(t);return 0;}
    *s=(BSGxmSurface){1,w,h,img,t,fb};
    return 1;
}
static void bs_switch_target(BSGxmRenderer* b,int target) {
    if(b->activeTarget==target)return;
    if(!b->frameIsActive)bs_unsupported("switch_target_without_frame");
    if(target>=0&&!bs_surface_valid(b,target)){
        bs_trace("NATIVE_GXM=surface_switch invalid_target skipped");
        return;
    }
    /* Chapter 5 issues surface_set_target during Draw/View. GameMaker
     * preserves the active camera viewport when changing the render target.
     * The old native path aborted because NanoVG holds an active save stack.
     * Save the view/GUI parameters, unwind that stack before nvgEndFrame,
     * then reconstruct it in the new frame. bs_endView/endGUI will still
     * restore exactly once as usual. */
    int wasView=b->viewActive,wasGui=b->guiActive;
    if(wasView||wasGui){
        nvgRestore(b->vg);
        b->viewActive=b->guiActive=0;
    }
    nvgEndFrame(b->vg);gxmEndFrame();
    if(target>=0) {
        BSGxmSurface *s=bs_surface(b,target);
        gxmBeginFrameEx(s->fb,0);
        nvgBeginFrame(b->vg,s->w,s->h,1);
    }else{
        gxmBeginFrame();
        nvgBeginFrame(b->vg,DISPLAY_WIDTH,DISPLAY_HEIGHT,1);
    }
    b->activeTarget=target;
    ++b->targetSwitches;
    if(wasView){
        int x=b->viewX,y=b->viewY,w=b->viewW,h=b->viewH;
        int px=b->portX,py=b->portY,pw=b->portW,ph=b->portH;
        float angle=b->viewAngle;
        if(w>0&&h>0&&pw>0&&ph>0){
            nvgSave(b->vg);nvgScissor(b->vg,px,py,pw,ph);
            nvgTranslate(b->vg,px,py);
            nvgScale(b->vg,(float)pw/w,(float)ph/h);
            if(angle!=0.0f){
                nvgTranslate(b->vg,w*.5f,h*.5f);
                nvgRotate(b->vg,-angle*BS_PI/180);
                nvgTranslate(b->vg,-w*.5f,-h*.5f);
            }
            nvgTranslate(b->vg,-x,-y);b->viewActive=1;
        }
    }else if(wasGui){
        int gw=b->guiW,gh=b->guiH,x=b->guiX,y=b->guiY;
        int w=b->guiPortW,h=b->guiPortH;
        nvgSave(b->vg);
        if(gw>0&&gh>0&&w>0&&h>0){
            nvgScissor(b->vg,x,y,w,h);nvgTranslate(b->vg,x,y);
            nvgScale(b->vg,(float)w/gw,(float)h/gh);
        }
        b->guiActive=1;
    }
    if(wasView||wasGui){
        static unsigned logged=0;
        if(logged++<16)bs_trace(
            "NATIVE_GXM=surface_switch inside_active_pass restore_camera=1");
    }
}
static void bs_init(Renderer* r, DataWin* dw) {
    BSGxmRenderer* b=BR(r);
    r->dataWin=dw;
    b->shaderLastRoom=-999;
    b->imageCount=dw->tpag.count;
    b->pageCount=dw->txtr.count;
    b->pageImages=(int*)calloc(b->pageCount,sizeof(int));
    b->pageWidth=(int*)calloc(b->pageCount,sizeof(int));
    b->pageHeight=(int*)calloc(b->pageCount,sizeof(int));
    b->pageBytes=(size_t*)calloc(b->pageCount,sizeof(size_t));
    b->pageLastUsed=(uint64_t*)calloc(b->pageCount,sizeof(uint64_t));
    extern int g_vitaActiveChapter;
    b->cacheBudget=VitaGpuRules_modernTextureCacheLimitBytes(g_vitaActiveChapter,NULL,0);
    b->images=(int*)calloc(b->imageCount,sizeof(int));
    b->width=(int*)calloc(b->imageCount,sizeof(int));
    b->height=(int*)calloc(b->imageCount,sizeof(int));
    if(!b->images||!b->width||!b->height||!b->pageImages||
       !b->pageWidth||!b->pageHeight||!b->pageBytes||!b->pageLastUsed)
       bs_unsupported("init_texture_cache_alloc");
    b->activeTarget=-1;b->displayW=960;b->displayH=544;
    b->decodedPage=-1;
    b->originalSpriteCount=dw->sprt.count;b->originalTpagCount=dw->tpag.count;
    b->writeR=b->writeG=b->writeB=b->writeA=1;
    b->borderLastRoom=-2;
    b->alphaEnable=0;b->blendEnable=1;b->alphaTestRef=0;
    b->blendMode=bm_normal;b->initialized=1;
    r->blendFactors=(BlendFactors){bm_src_alpha,bm_inv_src_alpha,bm_one,bm_inv_src_alpha};
    Matrix4f identity;Matrix4f_identity(&identity);
    r->gmlMatrices[MATRIX_WORLD]=identity;
}
static void bs_destroy(Renderer* r) {
    BSGxmRenderer* b=BR(r);
    /* Caller already fences GXM before destroying the runner. */
    b->frameIsActive=0;
    bs_release_retired(b);
    if(b->vg){
        for(unsigned i=0;i<b->snapshotRetiredCount;i++)
            nvgDeleteImage(b->vg,b->snapshotRetired[i].image);
        for(unsigned i=0;i<b->snapshotPoolCount;i++)
            nvgDeleteImage(b->vg,b->snapshotPool[i].image);
    }
    if (b->vg) {
        if(b->borderImage>0)nvgDeleteImage(b->vg,b->borderImage);
        if(b->borderOldImage>0)nvgDeleteImage(b->vg,b->borderOldImage);
        for(unsigned i=0;i<b->pageCount;i++)
            if(b->pageImages && b->pageImages[i]>0)nvgDeleteImage(b->vg,b->pageImages[i]);
        for(unsigned i=b->originalTpagCount;i<b->imageCount;i++)
            if(b->images && b->images[i]>0) nvgDeleteImage(b->vg,b->images[i]);
        for(int i=0;i<BS_MAX_SURFACES;i++) if(b->surfaces[i].live)bs_destroy_surface(b,i);
    }
    free(b->primVerts);
    free(b->decodedRgba);free(b->images);free(b->width);free(b->height);
    free(b->pageImages);free(b->pageWidth);free(b->pageHeight);
    free(b->pageBytes);free(b->pageLastUsed);free(b);
}

static void bs_beginFrame(Renderer* r,int32_t gw,int32_t gh,int32_t ww,int32_t wh) {
    BSGxmRenderer *b=BR(r);bs_require_renderer(b,"beginFrame_unbound");
    b->logicalW=gw;b->logicalH=gh;b->displayW=ww;b->displayH=wh;
    b->frameIsActive=1;b->draws=b->missing=b->targetSwitches=0;
    ++b->cacheFrame;
    /* Runner's world pass MUST write to application_surface, not the
     * display back buffer, which is only for post/GUI/compositing. */
    if(r->runner && r->runner->applicationSurfaceId>=0)
        bs_switch_target(b,r->runner->applicationSurfaceId);
    else bs_unsupported("beginFrame_application_surface_missing");
}
#include "native_borders.inc"

static void bs_endFrameInit(Renderer* r) {
    BSGxmRenderer *b=BR(r);b->worldDraws=b->draws;
    /* GameMaker blits application_surface without blending, so its alpha
     * never darkens the picture. The precompiled NanoVG image program always
     * multiplies RGB by the sampled alpha, so make the finished world surface
     * opaque first: an alpha-only write of 1 keeps every RGB byte. The
     * Chapter 3 green room clears its wall's alpha as a dest-alpha mask,
     * which otherwise turned the animated wall black. */
    if(r->runner&&r->runner->appSurfaceAutoDraw&&r->runner->appSurfaceEnabled&&
       r->runner->usingAppSurface&&bs_surface_valid(b,r->runner->applicationSurfaceId)){
        BSGxmSurface*s=bs_surface(b,r->runner->applicationSurfaceId);
        if(b->activeTarget!=r->runner->applicationSurfaceId)
            bs_switch_target(b,r->runner->applicationSurfaceId);
        if(b->viewActive||b->guiActive){
            nvgRestore(b->vg);b->viewActive=b->guiActive=0;
        }
        nvgSave(b->vg);
        nvgResetTransform(b->vg);nvgResetScissor(b->vg);
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeBlendFunc(b->vg,NVG_ONE,NVG_ZERO);
        nvgxmSetColorWriteMask(b->vg,8u);
        nvgBeginPath(b->vg);nvgRect(b->vg,0,0,(float)s->w,(float)s->h);
        nvgFillColor(b->vg,nvgRGBA(255,255,255,255));nvgFill(b->vg);
        nvgxmSetColorWriteMask(b->vg,(b->writeR?1u:0u)|(b->writeG?2u:0u)|
                                     (b->writeB?4u:0u)|(b->writeA?8u:0u));
        nvgRestore(b->vg);
    }
    if(b->activeTarget>=0)bs_switch_target(b,-1);
    bs_native_border_draw(b);
}
static void bs_endFrameEnd(Renderer* r) {
    BSGxmRenderer *b=BR(r);if(b->activeTarget>=0)bs_switch_target(b,-1);
    if(r->runner && r->runner->appSurfaceAutoDraw &&
       r->runner->appSurfaceEnabled && r->runner->usingAppSurface &&
       bs_surface_valid(b,r->runner->applicationSurfaceId)) {
        BSGxmSurface *s=bs_surface(b,r->runner->applicationSurfaceId);
        nvgSave(b->vg);
        float ratio=fminf((float)b->displayW/s->w,(float)b->displayH/s->h);
        extern int g_nativeStretchFill;
        float w=g_nativeStretchFill?b->displayW:s->w*ratio;
        float h=g_nativeStretchFill?b->displayH:s->h*ratio;
        extern int g_vitaDisplayZoom,g_vitaDisplayOffsetX,g_vitaDisplayOffsetY;
        int zoom=g_vitaDisplayZoom;
        if(zoom<25)zoom=25;
        if(zoom>200)zoom=200;
        w*=zoom/100.0f;h*=zoom/100.0f;
        float destX=(b->displayW-w)*.5f+g_vitaDisplayOffsetX;
        float destY=(b->displayH-h)*.5f+g_vitaDisplayOffsetY;
        /* Compositing safeguard: Tenna's light and other offscreen effects
         * can intentionally produce transparent pixels, but the VIDEO
         * framebuffer must not become a window into the Vita border.
         * The source application surface/its alpha remain unchanged. */
        nvgGlobalCompositeOperation(b->vg,NVG_SOURCE_OVER);
        nvgBeginPath(b->vg);nvgRect(b->vg,destX,destY,w,h);
        nvgFillColor(b->vg,nvgRGB(0,0,0));nvgFill(b->vg);
        /* Sharp Bilinear screen filter: linear sampling for the final
         * upscale only (vita_settings: g_vitaTextureLinearFilter). */
        {
            extern int g_vitaTextureLinearFilter;
            NVGXMtexture*handle=nvgxmImageHandle(b->vg,s->image);
            if(handle){
                SceGxmTextureFilter f=g_vitaTextureLinearFilter?
                    SCE_GXM_TEXTURE_FILTER_LINEAR:SCE_GXM_TEXTURE_FILTER_POINT;
                sceGxmTextureSetMinFilter(&handle->tex,f);
                sceGxmTextureSetMagFilter(&handle->tex,f);
            }
        }
        bs_image_rect(b,s->image,s->w,s->h,0,0,s->w,s->h,
                      destX,destY,w,h,1);
        nvgRestore(b->vg);
    }
}
static void bs_beginView(Renderer* r,int32_t x,int32_t y,int32_t w,int32_t h,
                         int32_t px,int32_t py,int32_t pw,int32_t ph,float angle){
    BSGxmRenderer* b=BR(r);bs_require_renderer(b,"beginView_unbound");
    if(b->viewActive || w<=0 || h<=0 || pw<=0 || ph<=0)
        bs_unsupported("beginView_invalid");
    r->CPortX=px;r->CPortY=py;r->CPortW=pw;r->CPortH=ph;
    b->viewX=x;b->viewY=y;b->viewW=w;b->viewH=h;
    b->portX=px;b->portY=py;b->portW=pw;b->portH=ph;b->viewAngle=angle;
    nvgSave(b->vg);nvgScissor(b->vg,px,py,pw,ph);
    nvgTranslate(b->vg,px,py);nvgScale(b->vg,(float)pw/w,(float)ph/h);
    if(angle!=0) {
        nvgTranslate(b->vg,w*.5f,h*.5f);nvgRotate(b->vg,-angle*BS_PI/180);
        nvgTranslate(b->vg,-w*.5f,-h*.5f);
    }
    nvgTranslate(b->vg,-x,-y);b->viewActive=1;
}
static void bs_endView(Renderer* r) {
    BSGxmRenderer* b=BR(r);
    if(b->viewActive){nvgRestore(b->vg);b->viewActive=0;}
}
static void bs_applyProjection(Renderer* r,const Matrix4f* view,const Matrix4f* proj) {
    if(view)r->gmlMatrices[MATRIX_VIEW]=*view;
    if(proj)r->gmlMatrices[MATRIX_PROJECTION]=*proj;
    /* Current camera transforms use beginView dimensions. */
}
static void bs_beginGUI(Renderer* r,int32_t gw,int32_t gh,int32_t x,int32_t y,
                        int32_t w,int32_t h,int32_t target) {
    BSGxmRenderer* b=BR(r);
    if(b->guiActive){bs_soft_unsupported("beginGUI_nested");nvgRestore(b->vg);b->guiActive=0;}
    if(target!=b->activeTarget)bs_switch_target(b,target);
    b->guiW=gw;b->guiH=gh;b->guiX=x;b->guiY=y;
    b->guiPortW=w;b->guiPortH=h;
    nvgSave(b->vg);
    if(gw>0&&gh>0&&w>0&&h>0){
        nvgScissor(b->vg,x,y,w,h);nvgTranslate(b->vg,x,y);
        nvgScale(b->vg,(float)w/gw,(float)h/gh);
    }
    b->guiActive=1;r->cameraCurrent=GUI_CAMERA;
}
static void bs_setGuiProjection(Renderer* r,int32_t gw,int32_t gh,int32_t pw,int32_t ph,bool userSurface) {
    (void)r;(void)gw;(void)gh;(void)pw;(void)ph;(void)userSurface;
}
static void bs_endGUI(Renderer* r) {
    BSGxmRenderer* b=BR(r);
    if(b->guiActive){nvgRestore(b->vg);b->guiActive=0;}
}
static void bs_drawSprite(Renderer* r,int32_t idx,float x,float y,float ox,float oy,
                          float xs,float ys,float angle,uint32_t tint,float alpha) {
    BSGxmRenderer* b=BR(r);
    if(idx<0 || (uint32_t)idx>=b->imageCount || !r->dataWin->tpag.items[idx].present)return;
    TexturePageItem *p=&r->dataWin->tpag.items[idx];

    if(angle==0.0f && xs==1.0f && ys==1.0f){
        bs_tpag_region(b,idx,0,0,p->sourceWidth,p->sourceHeight,
                       x+(float)p->targetX-ox,y+(float)p->targetY-oy,
                       p->targetWidth,p->targetHeight,0,0,0,alpha,tint);
        return;
    }
    nvgSave(b->vg);
    nvgTranslate(b->vg,x,y);
    if(angle!=0.0f)nvgRotate(b->vg,-angle*BS_PI/180.0f);
    if(xs!=1.0f||ys!=1.0f)nvgScale(b->vg,xs,ys);
    bs_tpag_region(b,idx,0,0,p->sourceWidth,p->sourceHeight,
                   (float)p->targetX-ox,(float)p->targetY-oy,
                   p->targetWidth,p->targetHeight,0,0,0,alpha,tint);
    nvgRestore(b->vg);
}
static void bs_drawSpritePart(Renderer* r,int32_t idx,int32_t sx,int32_t sy,
    int32_t sw,int32_t sh,float x,float y,float xs,float ys,float angle,
    float pivotX,float pivotY,uint32_t tint,float alpha) {
    BSGxmRenderer* b=BR(r);

    if(idx<0||(uint32_t)idx>=b->imageCount)return;
    bs_tpag_region(b,idx,sx,sy,sw,sh,x,y,sw*xs,sh*ys,angle,pivotX,pivotY,alpha,tint);
}
static void bs_drawRectangle(Renderer* r,float x1,float y1,float x2,float y2,
                             uint32_t color,float alpha,bool outline) {
    BSGxmRenderer* b=BR(r);nvgBeginPath(b->vg);nvgRect(b->vg,x1,y1,x2-x1,y2-y1);
    if(outline){nvgStrokeColor(b->vg,bs_col(color,alpha));nvgStrokeWidth(b->vg,1);nvgStroke(b->vg);}
    else{nvgFillColor(b->vg,bs_col(color,alpha));nvgFill(b->vg);}
    b->draws++;
}
/* Single native curve instead of GameMaker's 24+ separate fan triangles.
 * NanoVG radial color ramp retains center/perimeter colors; contour remains
 * elliptical and anti-aliased, including the TV light before Ch3's video. */
void BSNative_drawEllipseColor(Renderer*r,float cx,float cy,float rx,float ry,
                              uint32_t inner,uint32_t outer,bool outline){
    BSGxmRenderer*b=BR(r);
    if(!b||!b->vg||!isfinite(cx)||!isfinite(cy)||!isfinite(rx)||!isfinite(ry)||
       !isfinite(r->drawAlpha)||rx==0||ry==0)return;
    rx=fabsf(rx);ry=fabsf(ry);
    if(outline){
        nvgBeginPath(b->vg);nvgEllipse(b->vg,cx,cy,rx,ry);
        nvgStrokeWidth(b->vg,1.0f);
        nvgStrokeColor(b->vg,bs_col(outer,r->drawAlpha));
        nvgStroke(b->vg);
    }else{
        nvgSave(b->vg);
        nvgTranslate(b->vg,cx,cy);
        nvgScale(b->vg,rx,ry);
        nvgBeginPath(b->vg);nvgCircle(b->vg,0,0,1);
        if(inner==outer)nvgFillColor(b->vg,bs_col(inner,r->drawAlpha));
        else{
            NVGpaint gradient=nvgRadialGradient(b->vg,0,0,0,1,
                  bs_col(inner,r->drawAlpha),bs_col(outer,r->drawAlpha));
            nvgFillPaint(b->vg,gradient);
        }
        nvgFill(b->vg);nvgRestore(b->vg);
    }
    ++b->draws;
}
static uint32_t bs_mix_colors(uint32_t a,uint32_t b,float t);
static void bs_drawRectangleColor(Renderer* r,float x1,float y1,float x2,float y2,
    uint32_t c1,uint32_t c2,uint32_t c3,uint32_t c4,float alpha,bool outline) {
    BSGxmRenderer* b=BR(r);
    if(c1==c2&&c2==c3&&c3==c4){bs_drawRectangle(r,x1,y1,x2,y2,c1,alpha,outline);return;}
    /* In NanoVG the fixed GXP supports 2-stop linear gradient, not
     * independent four-corner vertex colors. Pick the strongest component
     * to preserve the most visible change without aborting the room. */
    uint32_t top=bs_mix_colors(c1,c2,.5f),bottom=bs_mix_colors(c4,c3,.5f);
    uint32_t left=bs_mix_colors(c1,c4,.5f),right=bs_mix_colors(c2,c3,.5f);
    int verticalScore=0,horizontalScore=0;
    for(int i=0;i<24;i+=8) {
        int d=(int)((top>>i)&255u)-(int)((bottom>>i)&255u);
        verticalScore+=d*d;
        d=(int)((left>>i)&255u)-(int)((right>>i)&255u);
        horizontalScore+=d*d;
    }
    nvgBeginPath(b->vg);nvgRect(b->vg,x1,y1,x2-x1,y2-y1);
    NVGpaint gradient=verticalScore>=horizontalScore?
      nvgLinearGradient(b->vg,x1,y1,x1,y2,bs_col(top,alpha),bs_col(bottom,alpha)):
      nvgLinearGradient(b->vg,x1,y1,x2,y1,bs_col(left,alpha),bs_col(right,alpha));
    if(outline){nvgStrokePaint(b->vg,gradient);nvgStroke(b->vg);}
    else{nvgFillPaint(b->vg,gradient);nvgFill(b->vg);}
    b->draws++;
}
static void bs_drawLine(Renderer* r,float x1,float y1,float x2,float y2,
                        float w,uint32_t c,float a) {
    BSGxmRenderer* b=BR(r);nvgBeginPath(b->vg);nvgMoveTo(b->vg,x1,y1);
    nvgLineTo(b->vg,x2,y2);nvgStrokeWidth(b->vg,w>0?w:1);
    nvgStrokeColor(b->vg,bs_col(c,a));nvgStroke(b->vg);b->draws++;
}
static void bs_drawTriangle(Renderer* r,float x1,float y1,float x2,float y2,float x3,float y3,
                           uint32_t c1,uint32_t c2,uint32_t c3,float a,bool outline) {
    BSGxmRenderer* b=BR(r);
    if(!isfinite(x1)||!isfinite(y1)||!isfinite(x2)||!isfinite(y2)||
       !isfinite(x3)||!isfinite(y3)||!isfinite(a))return;
    float e12=(x1-x2)*(x1-x2)+(y1-y2)*(y1-y2);
    float e23=(x2-x3)*(x2-x3)+(y2-y3)*(y2-y3);
    float e13=(x1-x3)*(x1-x3)+(y1-y3)*(y1-y3);
    /* Degenerate geometric triangles have no filled area; do not ask
     * NanoVG to normalize a zero-length gradient into NaNs, which showed
     * up as random dark wedges on native GXM in some animated scenes. */
    if(!outline && ((x2-x1)*(y3-y1)-(y2-y1)*(x3-x1))==0.0f)return;
    nvgBeginPath(b->vg);nvgMoveTo(b->vg,x1,y1);nvgLineTo(b->vg,x2,y2);
    nvgLineTo(b->vg,x3,y3);nvgClosePath(b->vg);
    if(c1==c2&&c2==c3){
        if(outline){nvgStrokeColor(b->vg,bs_col(c1,a));nvgStroke(b->vg);}
        else{nvgFillColor(b->vg,bs_col(c1,a));nvgFill(b->vg);}
    }else{
        /* GXP fixed NanoVG backend: approximate the three vertex colors
         * with a smooth, single-pass gradient along the dominant edge.
         * Vertex-exact interpolation needs a dedicated GXP program. */
        int d12=0,d23=0,d13=0;
        for(int i=0;i<24;i+=8){
            int q1=(c1>>i)&255,q2=(c2>>i)&255,q3=(c3>>i)&255;
            d12+=(q1-q2)*(q1-q2);
            d23+=(q2-q3)*(q2-q3);
            d13+=(q1-q3)*(q1-q3);
        }
        float ax=x1,ay=y1,bx=x2,by=y2;uint32_t ca=c1,cb=c2;
        int choose=0;int scores[3]={d12,d23,d13};
        float lengths[3]={e12,e23,e13};
        for(int j=1;j<3;j++)
            if(lengths[j]>=1.0f && (lengths[choose]<1.0f ||
                                   scores[j]>scores[choose]))choose=j;
        if(choose==1){ax=x2;ay=y2;bx=x3;by=y3;ca=c2;cb=c3;}
        else if(choose==2){ax=x1;ay=y1;bx=x3;by=y3;ca=c1;cb=c3;}
        /* A gradient edge shorter than one pixel gives NanoVG extreme
         * gradient coefficients: the GPU shaded those slivers black. */
        if(lengths[choose]<1.0f || !isfinite(lengths[choose])){
            /* Collapsed gradient edge: use center color rather than
             * feeding invalid shader coefficients to the GPU. */
            if(outline){nvgStrokeColor(b->vg,bs_col(c1,a));nvgStroke(b->vg);}
            else{nvgFillColor(b->vg,bs_col(c1,a));nvgFill(b->vg);}
        }else{
            NVGpaint paint=nvgLinearGradient(b->vg,ax,ay,bx,by,
                                             bs_col(ca,a),bs_col(cb,a));
            if(outline){nvgStrokePaint(b->vg,paint);nvgStroke(b->vg);}
            else{nvgFillPaint(b->vg,paint);nvgFill(b->vg);}
        }
        static unsigned reported=0;
        if(reported++<6)bs_trace(
           "NATIVE_GXM=drawTriangle_vertex_gradient mode=native_linear_approx");
    }
    b->draws++;
}
static void bs_drawLineColor(Renderer* r,float x1,float y1,float x2,float y2,
                            float width,uint32_t c1,uint32_t c2,float a){
    if(c1==c2){bs_drawLine(r,x1,y1,x2,y2,width,c1,a);return;}
    BSGxmRenderer*b=BR(r);
    nvgBeginPath(b->vg);nvgMoveTo(b->vg,x1,y1);nvgLineTo(b->vg,x2,y2);
    nvgStrokeWidth(b->vg,width>0?width:1);
    NVGpaint gradient=nvgLinearGradient(b->vg,x1,y1,x2,y2,
                                        bs_col(c1,a),bs_col(c2,a));
    nvgStrokePaint(b->vg,gradient);nvgStroke(b->vg);b->draws++;
}
static void bs_clearScreen(Renderer* r,uint32_t color,float alpha) {
    BSGxmRenderer* b=BR(r);
    bs_drawRectangle(r,0,0,b->activeTarget>=0?b->surfaces[b->activeTarget].w:DISPLAY_WIDTH,
      b->activeTarget>=0?b->surfaces[b->activeTarget].h:DISPLAY_HEIGHT,color,alpha,false);
}
static void bs_flush(Renderer* r) { (void)r; /* NanoVG submits on nvgEndFrame. */ }

/* Native render target surfaces: GXM framebuffer uses the image memory as color target. */
static void bs_surface_create_failed(int w,int h,const char*why) {
    /* A missing surface hides whole layers (the Chapter 5 Flowey tower), so
     * make every refusal visible, throttled. */
    static unsigned failures=0;
    if(++failures>24&&(failures%64)!=0)return;
    char line[140];
    snprintf(line,sizeof(line),
        "NATIVE_GXM=surface_create_failed size=%dx%d reason=%s count=%u",
        w,h,why,failures);
    bs_trace(line);
}
static int32_t bs_createSurface(Renderer* r,int32_t w,int32_t h) {
    BSGxmRenderer* b=BR(r);bs_require_renderer(b,"createSurface_unbound");
    for(int id=0;id<BS_MAX_SURFACES;id++){
        if(!b->surfaces[id].live){
            if(bs_create_surface_slot(b,id,w,h))return id;
            bs_surface_create_failed(w,h,"gpu_alloc");
            return -1;
        }
    }
    bs_surface_create_failed(w,h,"no_free_slot");
    return -1;
}
static bool bs_surfaceExists(Renderer* r,int32_t id){
#ifdef NATIVE_GXM_VIDEO
    if(id==VITA_VIDEO_SURFACE_ID)return VitaVideo_hasDecodedFrame();
#endif
    return bs_surface_valid(BR(r),id);
}
static int32_t bs_ensureApplicationSurface(Renderer* r,int32_t w,int32_t h){
    BSGxmRenderer* b=BR(r);
    if(!r->runner)return -1;
    int32_t id=r->runner->applicationSurfaceId;
    BSGxmSurface *s=bs_surface(b,id);
    if(s && s->w==w && s->h==h)return id;
    if(s) {
        if(b->activeTarget==id)bs_switch_target(b,-1);
        bs_destroy_surface(b,id);
        return bs_create_surface_slot(b,id,w,h)?id:-1;
    }
    return bs_createSurface(r,w,h);
}
static bool bs_setRenderTarget(Renderer* r,int32_t id,bool implicitApp) {
    BSGxmRenderer* b=BR(r);(void)implicitApp;
    if(!bs_surface_valid(b,id))return false;
    bs_switch_target(b,id);return true;
}
static float bs_getSurfaceWidth(Renderer* r,int32_t id){
#ifdef NATIVE_GXM_VIDEO
    if(id==VITA_VIDEO_SURFACE_ID)return VitaVideo_getWidth();
#endif
    BSGxmSurface *s=bs_surface(BR(r),id);return s?(float)s->w:0;
}
static float bs_getSurfaceHeight(Renderer* r,int32_t id){
#ifdef NATIVE_GXM_VIDEO
    if(id==VITA_VIDEO_SURFACE_ID)return VitaVideo_getHeight();
#endif
    BSGxmSurface *s=bs_surface(BR(r),id);return s?(float)s->h:0;
}
static void bs_surfaceFree(Renderer* r,int32_t id) {
    BSGxmRenderer* b=BR(r);if(b->activeTarget==id)bs_switch_target(b,-1);
    bs_destroy_surface(b,id);
}
static void bs_surfaceResize(Renderer* r,int32_t id,int32_t w,int32_t h){
    BSGxmRenderer* b=BR(r);
    if(!bs_surface_valid(b,id))return;
    if(b->activeTarget==id)bs_switch_target(b,-1);
    bs_destroy_surface(b,id);
    if(!bs_create_surface_slot(b,id,w,h))bs_unsupported("surfaceResize_alloc_failed");
}
static void bs_drawSurface(Renderer* r,int32_t id,int32_t sx,int32_t sy,
  int32_t sw,int32_t sh,float x,float y,float xs,float ys,float angle,uint32_t tint,float a) {
    BSGxmRenderer* b=BR(r);
#ifdef NATIVE_GXM_VIDEO
    if(id==VITA_VIDEO_SURFACE_ID){
        int img=VitaVideo_image();
        float vw=VitaVideo_getWidth(),vh=VitaVideo_getHeight();
        /* GameMaker draw_surface passes sw=sh=-1 to request the FULL
         * source surface. The old guard discarded ALL video draws. */
        if(sw<=0)sw=(int)vw-sx;
        if(sh<=0)sh=(int)vh-sy;
        if(img>0&&vw>0&&vh>0&&sw>0&&sh>0){
            static int traced=0;
            if(traced++<5)bs_trace("NATIVE_GXM=video draw_surface video_frame_to_nvg=1");
            nvgSave(b->vg);nvgTranslate(b->vg,x,y);
            if(angle!=0)nvgRotate(b->vg,-angle*BS_PI/180);
            bs_image_tinted(b,img,(int)vw,(int)vh,sx,sy,sw,sh,
                             0,0,sw*xs,sh*ys,tint,a);
            nvgRestore(b->vg);
        }
        return;
    }
#endif
    BSGxmSurface* s=bs_surface(b,id);
    if(!s)return;
    if(b->activeTarget==id){bs_soft_unsupported("drawSurface_feedback");return;}
    nvgSave(b->vg);nvgTranslate(b->vg,x,y);nvgRotate(b->vg,-angle*BS_PI/180);
    bs_image_tinted(b,s->image,s->w,s->h,sx,sy,sw,sh,
                    0,0,sw*xs,sh*ys,tint,a);
    nvgRestore(b->vg);
}
static void bs_drawSurfaceColor(Renderer* r,int32_t id,int32_t sx,int32_t sy,
   int32_t sw,int32_t sh,float x,float y,float xs,float ys,float angle,
   uint32_t c1,uint32_t c2,uint32_t c3,uint32_t c4,float a) {
    if(c1!=c2||c2!=c3||c3!=c4) {
        static int once=0;
        if(!once++)bs_trace("NATIVE_GXM=drawSurfaceColor_gradient fallback=average_tint GXP_pending");
    }
    /* Texture samples cannot be modulated by four independent vertex tints
     * without a shader; keep the surface visible instead of aborting. */
    uint32_t top=bs_mix_colors(c1,c2,.5f);
    uint32_t bottom=bs_mix_colors(c4,c3,.5f);
    uint32_t tint=bs_mix_colors(top,bottom,.5f);
    bs_drawSurface(r,id,sx,sy,sw,sh,x,y,xs,ys,angle,tint,a);
}
static void bs_drawSurfaceTiled(Renderer* r,int32_t id,float x,float y,float xs,float ys,
                               float rw,float rh,uint32_t tint,float a) {
    BSGxmRenderer* b=BR(r);BSGxmSurface* s=bs_surface(b,id);
    if(!s || xs==0 || ys==0)return;
    float tw=fabsf(s->w*xs),th=fabsf(s->h*ys);
    if(tw<.01f||th<.01f)return;
    nvgSave(b->vg);
    nvgScissor(b->vg,0,0,rw,rh);
    BSGxmVisible vis=bs_visible_local(b);
    int x0=0,x1=0,y0=0,y1=0;
    bs_tile_index_span(x,tw,rw,vis.valid?vis.minX-tw:-INFINITY,
                       vis.valid?vis.maxX+tw:INFINITY,&x0,&x1);
    bs_tile_index_span(y,th,rh,vis.valid?vis.minY-th:-INFINITY,
                       vis.valid?vis.maxY+th:INFINITY,&y0,&y1);
    int totalX=0,totalY=0;
    bs_tile_index_span(x,tw,rw,-INFINITY,INFINITY,&totalX,&totalY);
    int fullX=totalY;
    bs_tile_index_span(y,th,rh,-INFINITY,INFINITY,&totalX,&totalY);
    int fullY=totalY;
    b->tiledCalls++;
    b->tileIterationsAvoided+=(uint64_t)fullX*fullY-
                     (uint64_t)(x1-x0)*(y1-y0);
    for(int iy=y0;iy<y1;iy++)for(int ix=x0;ix<x1;ix++){
        bs_image_tinted(b,s->image,s->w,s->h,0,0,s->w,s->h,
            x+ix*tw,y+iy*th,tw,th,tint,a);
        b->tileIterationsExecuted++;
    }
    nvgRestore(b->vg);
}
static bool bs_surfaceGetPixels(Renderer* r,int32_t id,uint8_t* outRGBA) {
    BSGxmRenderer* b=BR(r);BSGxmSurface* s=bs_surface(b,id);
    if(!s || !outRGBA)return false;
    bs_snapshot_sync(b,1);
    /* render target layout is linear, GXM stride 8-pixel aligned */
    unsigned pitch=(unsigned)((s->w+7)&~7);
    for(int y=0;y<s->h;y++)
       memcpy(outRGBA+(size_t)y*s->w*4,s->texture->data+(size_t)y*pitch*4,(size_t)s->w*4);
    return true;
}
static void bs_surfaceCopy(Renderer* r,int32_t dest,int32_t dx,int32_t dy,
                          int32_t src,int32_t sx,int32_t sy,int32_t sw,int32_t sh,bool part) {
    BSGxmRenderer* b=BR(r);
    if(!bs_surface_valid(b,dest)||!bs_surface_valid(b,src))return;
    if(dest==src){bs_soft_unsupported("surfaceCopy_self");return;}
    int prev=b->activeTarget;
    if(prev==src){bs_soft_unsupported("surfaceCopy_active_source");return;}
    bs_switch_target(b,dest);
    if(!part){sx=0;sy=0;sw=b->surfaces[src].w;sh=b->surfaces[src].h;}
    bs_image_rect(b,b->surfaces[src].image,b->surfaces[src].w,b->surfaces[src].h,
                  sx,sy,sw,sh,dx,dy,sw,sh,1);
    bs_switch_target(b,prev);
}

/* GXM render-target data is GPU-owned. Close and finish the scene before
 * reading it on the CPU. Resume NanoVG using the same active target and pass.
 * This is a correctness path for rare sprite_create_from_surface calls, not
 * the optimized implementation (future path can use a GPU blit). */
static void bs_snapshot_sync(BSGxmRenderer *b,int cpuWait) {
    if (!b->frameIsActive || !b->vg) return;
    nvgEndFrame(b->vg);
    gxmEndFrame();
    /* Snapshot copies use a producer fragment sync object; do not block
     * the CPU on the entire GXM context. Readback callers still wait. */
    if(cpuWait){g_gxmRenderStage="gpu_finish";sceGxmFinish(gxmGetWindow()->context);g_gxmRenderStage=NULL;}
    int w=DISPLAY_WIDTH,h=DISPLAY_HEIGHT;
    if(b->activeTarget>=0){
        BSGxmSurface *s=bs_surface(b,b->activeTarget);
        if(!s)bs_unsupported("sprite_snapshot_active_target_missing");
        gxmBeginFrameEx(s->fb,0);
        w=s->w;h=s->h;
    } else gxmBeginFrame();
    nvgBeginFrame(b->vg,w,h,1.0f);
    if(b->viewActive){
        nvgSave(b->vg);
        nvgScissor(b->vg,b->portX,b->portY,b->portW,b->portH);
        nvgTranslate(b->vg,b->portX,b->portY);
        nvgScale(b->vg,(float)b->portW/b->viewW,(float)b->portH/b->viewH);
        if(b->viewAngle){
            nvgTranslate(b->vg,b->viewW*.5f,b->viewH*.5f);
            nvgRotate(b->vg,-b->viewAngle*BS_PI/180);
            nvgTranslate(b->vg,-b->viewW*.5f,-b->viewH*.5f);
        }
        nvgTranslate(b->vg,-b->viewX,-b->viewY);
    }
    if(b->guiActive){
        nvgSave(b->vg);
        if(b->guiW>0&&b->guiH>0&&b->guiPortW>0&&b->guiPortH>0){
            nvgScissor(b->vg,b->guiX,b->guiY,b->guiPortW,b->guiPortH);
            nvgTranslate(b->vg,b->guiX,b->guiY);
            nvgScale(b->vg,(float)b->guiPortW/b->guiW,
                            (float)b->guiPortH/b->guiH);
        }
    }
    if(!b->blendEnable) nvgGlobalCompositeBlendFunc(b->vg,NVG_ONE,NVG_ZERO);
    else if(b->blendMode==bm_add) nvgGlobalCompositeOperation(b->vg,NVG_LIGHTER);
    else nvgGlobalCompositeOperation(b->vg,NVG_SOURCE_OVER);
}
static int bs_grow_tpag_cache(BSGxmRenderer *b,unsigned target) {
    if(target<=b->imageCount)return 1;
    /* This must be relative to each game's actual original TPAG count.
     * Chapter5 ships 23,859 embedded atlas items: the legacy 2,048
     * absolute limit made EVERY sprite_create_from_surface fail *after*
     * synchronizing and copying the source GPU surface. Reserve additional
     * slots in small batches to amortize reallocations. 512 dynamic slots
     * are enough for game scripts while preventing unbounded memory growth. */
    if(target>b->originalTpagCount+512u)return 0;
    unsigned capacity=(target+63u)&~63u;
    if(capacity>b->originalTpagCount+512u)
        capacity=b->originalTpagCount+512u;
    int *imgs=(int*)calloc(capacity,sizeof(int));
    int *ws=(int*)calloc(capacity,sizeof(int));
    int *hs=(int*)calloc(capacity,sizeof(int));
    if(!imgs||!ws||!hs){free(imgs);free(ws);free(hs);return 0;}
    memcpy(imgs,b->images,b->imageCount*sizeof(int));
    memcpy(ws,b->width,b->imageCount*sizeof(int));
    memcpy(hs,b->height,b->imageCount*sizeof(int));
    free(b->images);free(b->width);free(b->height);
    b->images=imgs;b->width=ws;b->height=hs;b->imageCount=capacity;
    return 1;
}

static int32_t bs_createSpriteFromSurface(Renderer* r,int32_t id,int32_t x,int32_t y,int32_t w,
                                int32_t h,bool removeback,bool smooth,int32_t ox,int32_t oy) {
    BSGxmRenderer *b=BR(r);
    DataWin* dw=r->dataWin;
    (void)removeback; /* Matches existing GL runner's unimplemented option. */
    (void)smooth;
    BSGxmSurface *src=bs_surface(b,id);
    if(!src || x<0 || y<0 || w<=0 || h<=0 || w>1024 || h>1024 ||
       x>src->w-w || y>src->h-h) return -1;
    const int tw=bs_pot(w),th=bs_pot(h);
    /* The sprite is a SNAPSHOT and must retain its texels after the source
     * surface is subsequently redrawn. Keep a private image per capture. */
    const uint64_t snapshotStartedUs=sceKernelGetProcessTimeWide();
    bs_snapshot_sync(b,0);
    ++b->snapshotCount;
    if(b->snapshotCount<=12 || (b->snapshotCount%128)==0){
        int owner=(r->runner&&r->runner->vmContext)?
            r->runner->vmContext->currentEventObjectIndex:-1;
        const char*obj=(owner>=0&&r->dataWin&&
                        (uint32_t)owner<r->dataWin->objt.count)?
             r->dataWin->objt.objects[owner].name:NULL;
        char msg[210];
        snprintf(msg,sizeof(msg),
           "NATIVE_GXM=snapshot_source room=%d obj=%d name=%.65s "
           "src_surface=%d region=%d,%d,%dx%d count=%lu",
           r->runner?r->runner->currentRoomIndex:-1,
           owner,obj?obj:"<unknown>",id,x,y,w,h,
           (unsigned long)b->snapshotCount);
        bs_trace(msg);
    }
    /* Snapshots are RGBA8888 (U8U8U8U8_ABGR), the render target's own
     * layout: the transfer engine has no RGBA-ordered 4444 format, and
     * U4U4U4U4_ABGR texels sampled by the U4U4U4U4_RGBA image swapped
     * channels (pink text in the Chapter 0 shrink transition).
     * Native GXM transfer engine: render target -> sprite.
     * The GPU performs the copy and channel conversion without mapping the
     * framebuffer as CPU pixels, which is critical for room112's 3 snapshots
     * every frame. Handle failures with the original CPU path; never return
     * an uninitialized sprite. Previous bs_snapshot_sync guarantees producer
     * completion on physical hardware before the transfer reads this target.
     * Packed destination is zero-initialized so POT padding remains clear. */
    int img=bs_snapshot_pool_take(b,tw,th);
    if(!img)img=nvgCreateImageRGBA(b->vg,tw,th,NVG_IMAGE_NEAREST,NULL);
    int copied=0,transferStatus=-1;
    if(img>0){
        NVGXMtexture *dst=nvgxmImageHandle(b->vg,img);
        if(dst && dst->data && src->texture && src->texture->data){
            transferStatus=sceGxmTransferCopy(
               (uint32_t)w,(uint32_t)h,0,0,
               SCE_GXM_TRANSFER_COLORKEY_NONE,
               SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
               SCE_GXM_TRANSFER_LINEAR,src->texture->data,
               (uint32_t)x,(uint32_t)y,(int32_t)(((src->w+7)&~7)*4),
               SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
               SCE_GXM_TRANSFER_LINEAR,dst->data,
               0,0,(int32_t)(((tw+7)&~7)*4),
               src->fb ? src->fb->gxm_color_surfaces[
                   src->fb->gxm_back_buffer_index].sync_object : NULL,
               SCE_GXM_TRANSFER_FRAGMENT_SYNC,NULL);
            if(transferStatus>=0){
                sceGxmTransferFinish();
                copied=1;
            }
        }
    }
    if(!copied){
        /* Transfer setup failed: fall back to completed producer GPU writes
         * before CPU memcpy. Never read unretired GXM pixels. */
        {g_gxmRenderStage="gpu_finish";sceGxmFinish(gxmGetWindow()->context);g_gxmRenderStage=NULL;}
        if(img>0){nvgDeleteImage(b->vg,img);img=0;}
        uint8_t *rgba=(uint8_t*)calloc((size_t)tw*th,4);
        if(!rgba)return -1;
        const size_t pitch=(size_t)((src->w+7)&~7)*4;
        for(int row=0;row<h;row++){
            memcpy(rgba+(size_t)row*tw*4,
                   src->texture->data+((size_t)(y+row)*pitch+(size_t)x*4),
                   (size_t)w*4);
        }
        img=nvgCreateImageRGBA(b->vg,tw,th,NVG_IMAGE_NEAREST,rgba);
        free(rgba);
    }
    if(img<=0)return -1;
    {
        static unsigned gpuCopies=0,cpuCopies=0;
        static uint64_t gpuElapsedTotal=0,cpuElapsedTotal=0;
        uint64_t elapsed=sceKernelGetProcessTimeWide()-snapshotStartedUs;
        if(copied){++gpuCopies;gpuElapsedTotal+=elapsed;}
        else{++cpuCopies;cpuElapsedTotal+=elapsed;}
        if(gpuCopies+cpuCopies<=6||(gpuCopies+cpuCopies)%256==0){
            char msg[162];
            snprintf(msg,sizeof(msg),
              "NATIVE_GXM=snapshot_transfer mode=%s result=0x%08X "
              "gpu=%u cpu=%u rect=%dx%d",
              copied?"gpu":"cpu",(unsigned)transferStatus,
              gpuCopies,cpuCopies,w,h);
            bs_trace(msg);
            snprintf(msg,sizeof(msg),
              "NATIVE_GXM=snapshot_timing gpu_avg_us=%llu cpu_avg_us=%llu "
              "last_us=%llu",
              (unsigned long long)(gpuCopies?gpuElapsedTotal/gpuCopies:0),
              (unsigned long long)(cpuCopies?cpuElapsedTotal/cpuCopies:0),
              (unsigned long long)elapsed);
            bs_trace(msg);
            snprintf(msg,sizeof(msg),
              "NATIVE_GXM=snapshot_pool reusable=%u retired=%u hits=%u "
              "alloc=%u drops=%u bytes=%lu",
              b->snapshotPoolCount,b->snapshotRetiredCount,
              b->snapshotReuses,b->snapshotAllocations,b->snapshotDrops,
              (unsigned long)b->snapshotPoolBytes);
            bs_trace(msg);
        }
    }
    unsigned tid=dw->tpag.count;
    for(unsigned i=b->originalTpagCount;i<dw->tpag.count;i++) {
        if(!dw->tpag.items[i].present){tid=i;break;}
    }
    if(!bs_grow_tpag_cache(b,tid+1)){
        nvgDeleteImage(b->vg,img);return -1;
    }
    if(tid==dw->tpag.count){
        TexturePageItem *items=(TexturePageItem*)realloc(dw->tpag.items,
                (size_t)(tid+1)*sizeof(TexturePageItem));
        if(!items){nvgDeleteImage(b->vg,img);return -1;}
        dw->tpag.items=items;dw->tpag.count=tid+1;
    }
    TexturePageItem *tp=&dw->tpag.items[tid];
    memset(tp,0,sizeof(*tp));
    tp->present=true;
    tp->texturePageId=-1; /* Externally owned GXM image, not an embedded TXTR. */
    tp->sourceWidth=tp->targetWidth=tp->boundingWidth=(uint16_t)w;
    tp->sourceHeight=tp->targetHeight=tp->boundingHeight=(uint16_t)h;
    b->images[tid]=img;b->width[tid]=tw;b->height[tid]=th;
    unsigned sid=DataWin_allocSpriteSlot(dw,b->originalSpriteCount);
    Sprite *sprite=&dw->sprt.sprites[sid];
    sprite->present=true;sprite->width=(unsigned)w;sprite->height=(unsigned)h;
    sprite->originX=ox;sprite->originY=oy;sprite->textureCount=1;
    sprite->tpagIndices=(int32_t*)malloc(sizeof(int32_t));
    if(!sprite->tpagIndices){
        sprite->textureCount=0;sprite->present=false;
        b->images[tid]=0;tp->present=false;
        nvgDeleteImage(b->vg,img);return -1;
    }
    sprite->tpagIndices[0]=(int32_t)tid;
    ++b->dynamicSprites;
    ++b->dynamicCreateTotal;
    if(b->dynamicCreateTotal<=8u || !(b->dynamicCreateTotal%256u)){
        char entry[195];
        snprintf(entry,sizeof(entry),
            "NATIVE_GXM=dynamic_sprite created=%u tpag=%u source=%d "
            "rect=%d,%d,%d,%d active=%u cumulative=%u",
            sid,tid,id,x,y,w,h,b->dynamicSprites,b->dynamicCreateTotal);
        bs_trace(entry);
    }
    return (int32_t)sid;
}
static void bs_deleteSprite(Renderer* r,int32_t id) {
    BSGxmRenderer *b=BR(r);
    DataWin* dw=r->dataWin;
    if(id<0 || (unsigned)id<b->originalSpriteCount ||
       (unsigned)id>=dw->sprt.count)return;
    Sprite *spr=&dw->sprt.sprites[id];
    if(!spr->textureCount)return;
    for(unsigned i=0;i<spr->textureCount;i++){
        int ti=spr->tpagIndices[i];
        if(ti<0 || (unsigned)ti<b->originalTpagCount ||
           (unsigned)ti>=b->imageCount)continue;
        bs_snapshot_pool_retire(b,b->images[ti],b->width[ti],b->height[ti]);
        b->images[ti]=0;b->width[ti]=b->height[ti]=0;
        TexturePageItem* tp=&dw->tpag.items[ti];
        memset(tp,0,sizeof(*tp));tp->texturePageId=-1;
    }
    free(spr->tpagIndices);
    const char *name=spr->name;
    memset(spr,0,sizeof(*spr));spr->name=name;
    if(b->dynamicSprites>0)--b->dynamicSprites;
}
static uint32_t bs_surfaceGetTexture(Renderer* r,int32_t id){
    BSGxmSurface* s=bs_surface(BR(r),id);
    return s?(uint32_t)(0x70000000u|(unsigned)id):0u;
}

/* Texture handle table. High nibble differentiates game page item and GXM surfaces. */
static uint32_t bs_spriteGetTexture(Renderer* r,int32_t idx){
    BSGxmRenderer* b=BR(r);
    if(idx<0||(uint32_t)idx>=b->imageCount)return 0;
    if(!bs_ensure_image(b,idx))return 0;
    return 0x40000000u | (uint32_t)idx;
}
static int bs_resolve_texture(BSGxmRenderer* b,uint32_t handle,int* image,int* w,int* h) {
    if((handle & 0xF0000000u)==0x70000000u){
        BSGxmSurface *s=bs_surface(b,(int)(handle & 0xFFFFFFu));
        if(!s)return 0;
        *image=s->image;*w=s->w;*h=s->h;return 1;
    }
    if((handle & 0xF0000000u)==0x40000000u){
        unsigned id=handle&0xFFFFFFu;
        if(id>=b->imageCount)return 0;
        int img=bs_ensure_image(b,(int)id);
        if(img<=0)return 0;
        *image=img;*w=b->width[id];*h=b->height[id];return 1;
    }
    return 0;
}
static float bs_textureGetTexelWidth(Renderer* r,uint32_t h){
    int img,w,z;return bs_resolve_texture(BR(r),h,&img,&w,&z)&&w?1.0f/w:0;
}
static float bs_textureGetTexelHeight(Renderer* r,uint32_t h){
    int img,w,z;return bs_resolve_texture(BR(r),h,&img,&w,&z)&&z?1.0f/z:0;
}
static bool bs_textureGetUVs(Renderer* r,uint32_t h,float *uv){
    BSGxmRenderer*b=BR(r);if(!uv)return false;
    if((h&0xF0000000u)==0x70000000u){
        if(!bs_surface_valid(b,(int)(h&0xFFFFFFu)))return false;
        uv[0]=0;uv[1]=0;uv[2]=1;uv[3]=1;return true;
    }
    if((h&0xF0000000u)!=0x40000000u)return false;
    unsigned i=h&0xFFFFFFu;
    if(i>=b->imageCount || !bs_ensure_image(b,(int)i))return false;
    TexturePageItem*t=&r->dataWin->tpag.items[i];
    float ox=i<b->originalTpagCount?(float)t->sourceX:0;
    float oy=i<b->originalTpagCount?(float)t->sourceY:0;
    uv[0]=ox/b->width[i];uv[1]=oy/b->height[i];
    uv[2]=(ox+t->sourceWidth)/b->width[i];
    uv[3]=(oy+t->sourceHeight)/b->height[i];
    return true;
}
static void bs_textureSetStage(Renderer* r,int32_t slot,uint32_t tex){
    BSGxmRenderer*b=BR(r);
    if(!b||!b->vg||slot<1||slot>3)return;
    int img=0,w=0,h=0;
    if(tex && !bs_resolve_texture(b,tex,&img,&w,&h))img=0;
    nvgxmSetRuntimeSamplerImage(b->vg,slot,img);
    static unsigned logged=0;
    if(logged++<25){
        char line[162];snprintf(line,sizeof(line),
           "NATIVE_GXM=shader_sampler stage=%d image=%d tex_handle=0x%08x size=%dx%d",
           slot,img,tex,w,h);
        bs_trace(line);
    }
}
static BlendFactors bs_gpuGetBlendFactors(Renderer* r){return r->blendFactors;}
static int32_t bs_gpuGetBlendMode(Renderer* r){return BR(r)->blendMode;}
/*
 * Follow GLCommon_blendModeToEquation/SFactor/DFactor from the
 * playable Modern-GL backend, not the descriptive enum names:
 * - bm_subtract: GL_FUNC_ADD, ZERO, ONE_MINUS_SRC_COLOR
 * - bm_reverse_subtract: GL_FUNC_REVERSE_SUBTRACT, SRC_ALPHA, ONE
 * - bm_min: GL_MIN, ONE, ONE
 * - bm_max: GL_FUNC_ADD, SRC_ALPHA, ONE_MINUS_SRC_COLOR
 * GXM blend equations are compiled into a fragment-patcher variant;
 * per-draw equations must be captured at NanoVG call-creation time.
 */
static void bs_gpuSetBlendMode(Renderer* r,int32_t mode) {
    BSGxmRenderer*b=BR(r);
    b->blendMode=mode;
    static uint32_t loggedModes=0;
    if(mode>=0&&mode<31 && !(loggedModes&(1u<<mode))){
        loggedModes|=1u<<mode;
        char line[148];
        snprintf(line,sizeof(line),
           "NATIVE_GXM=blend_mode mode=%ld native_gxm=equation_and_factors",
           (long)mode);
        bs_trace(line);
    }
    if(!b->blendEnable){
       nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
       nvgGlobalCompositeBlendFunc(b->vg,NVG_ONE,NVG_ZERO);
       return;
    }
    if(mode==bm_normal){
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeOperation(b->vg,NVG_SOURCE_OVER);
    }else if(mode==bm_add){
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeOperation(b->vg,NVG_LIGHTER);
    }else if(mode==bm_subtract){
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeBlendFuncSeparate(b->vg,
            NVG_ZERO,NVG_ONE_MINUS_SRC_COLOR,
            NVG_ZERO,NVG_ONE_MINUS_SRC_COLOR);
    }else if(mode==bm_reverse_subtract){
        nvgxmSetBlendEquation(b->vg,
            SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT,
            SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT);
        nvgGlobalCompositeBlendFuncSeparate(b->vg,
            NVG_SRC_ALPHA,NVG_ONE,NVG_SRC_ALPHA,NVG_ONE);
    }else if(mode==bm_min){
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_MIN,SCE_GXM_BLEND_FUNC_MIN);
        nvgGlobalCompositeBlendFunc(b->vg,NVG_ONE,NVG_ONE);
    }else if(mode==bm_max){
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeBlendFuncSeparate(b->vg,
            NVG_SRC_ALPHA,NVG_ONE_MINUS_SRC_COLOR,
            NVG_SRC_ALPHA,NVG_ONE_MINUS_SRC_COLOR);
    }else{
        char line[136];
        snprintf(line,sizeof(line),
            "NATIVE_GXM=blend_unknown mode=%ld fallback=source_over",(long)mode);
        bs_trace(line);
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeOperation(b->vg,NVG_SOURCE_OVER);
    }
}
static int bs_factor(int f){
    switch(f){
        case bm_zero:return NVG_ZERO;
        case bm_one:return NVG_ONE;
        case bm_src_color:return NVG_SRC_COLOR;
        case bm_inv_src_color:return NVG_ONE_MINUS_SRC_COLOR;
        case bm_src_alpha:return NVG_SRC_ALPHA;
        case bm_inv_src_alpha:return NVG_ONE_MINUS_SRC_ALPHA;
        case bm_dest_alpha:return NVG_DST_ALPHA;
        case bm_inv_dest_alpha:return NVG_ONE_MINUS_DST_ALPHA;
        case bm_dest_color:return NVG_DST_COLOR;
        case bm_inv_dest_color:return NVG_ONE_MINUS_DST_COLOR;
        case bm_src_alpha_sat:return NVG_SRC_ALPHA_SATURATE;
        default:bs_soft_unsupported("blend_factor");return NVG_ONE;
    }
}
static void bs_gpuSetBlendModeExt(Renderer* r,int32_t sr,int32_t dr,int32_t sa,int32_t da) {
    BSGxmRenderer*b=BR(r);
    r->blendFactors=(BlendFactors){sr,dr,sa,da};b->blendMode=bm_complex;
    nvgGlobalCompositeBlendFuncSeparate(b->vg,bs_factor(sr),bs_factor(dr),
                                                   bs_factor(sa),bs_factor(da));
}
static void bs_gpuSetBlendEnable(Renderer* r,bool enabled) {
    BSGxmRenderer*b=BR(r);b->blendEnable=enabled;
    if(!enabled) {
        nvgxmSetBlendEquation(b->vg,SCE_GXM_BLEND_FUNC_ADD,SCE_GXM_BLEND_FUNC_ADD);
        nvgGlobalCompositeBlendFunc(b->vg,NVG_ONE,NVG_ZERO);
    } else if(b->blendMode==bm_complex) {
        BlendFactors f=r->blendFactors;
        nvgGlobalCompositeBlendFuncSeparate(b->vg,bs_factor(f.src),bs_factor(f.dst),
                                            bs_factor(f.srcAlpha),bs_factor(f.dstAlpha));
    } else bs_gpuSetBlendMode(r,b->blendMode);
}
static bool bs_gpuGetBlendEnable(Renderer* r){return BR(r)->blendEnable!=0;}
/*
 * On VitaGL GL_GREATER tests fragment alpha > ref / 255.0f.
 * Our current precompiled GXM/NanoVG fragment programs cannot issue a
 * texture-alpha-dependent discard. Track GameMaker state exactly, reject
 * primitives whose UNIFORM alpha cannot pass, and preserve ordinary
 * source-over blending for transparent sprites (binary-alpha safe path).
 * Never change the game's blend factors or destination-alpha write mask.
 * A future custom GXP fragment program is needed for per-texel alpha clip.
 */
static int bs_alpha_uniform_visible(BSGxmRenderer* b,float alpha){
    return !b->alphaEnable || bs_clamp01(alpha)*255.0f>(float)b->alphaTestRef;
}
static void bs_gpuSetAlphaTestEnable(Renderer* r,bool enable){
    BSGxmRenderer*b=BR(r);
    if(b->alphaEnable==(int)enable)return;
    b->alphaEnable=enable;
    ++b->alphaTestChanges;
    if(b->alphaTestChanges<=16){
        char line[176];
        snprintf(line,sizeof(line),
            "NATIVE_GXM=alpha_test enabled=%d ref=%u mode=cpu_uniform_binary_alpha_compat change=%u",
            b->alphaEnable,b->alphaTestRef,b->alphaTestChanges);
        bs_trace(line);
    }
}
static void bs_gpuSetAlphaTestRef(Renderer* r,uint8_t ref){
    BSGxmRenderer*b=BR(r);
    if(b->alphaTestRef==ref)return;
    b->alphaTestRef=ref;
    ++b->alphaTestRefChanges;
    if(b->alphaTestRefChanges<=16){
        char line[144];
        snprintf(line,sizeof(line),
            "NATIVE_GXM=alpha_test_ref value=%u fraction=%.5f change=%u",
            ref,(float)ref/255.0f,b->alphaTestRefChanges);
        bs_trace(line);
    }
}
static void bs_gpuSetColorWriteEnable(Renderer* r,bool a,bool b,bool c,bool d){
    BSGxmRenderer *g=BR(r);g->writeR=a;g->writeG=b;g->writeB=c;g->writeA=d;
    nvgxmSetColorWriteMask(g->vg,(a?1u:0u)|(b?2u:0u)|(c?4u:0u)|(d?8u:0u));
    if(!(a&&b&&c&&d))++g->maskFallbacks;
}
static void bs_gpuGetColorWriteEnable(Renderer*r,bool*a,bool*b,bool*c,bool*d) {
    BSGxmRenderer *g=BR(r);if(a)*a=g->writeR;if(b)*b=g->writeG;
    if(c)*c=g->writeB;
    if(d)*d=g->writeA;
}
/* Track original GameMaker state while fixed native GXP is in use.
 * Without compiled per-shader GXP fog-depth and GameMaker pixel effects
 * are intentionally NOT presented as supported. */
static void bs_gpuSetFog(Renderer*r,bool enable,uint32_t color){
    BSGxmRenderer*b=BR(r);
    if(b->fogEnabled==(int)enable && b->fogColor==color)return;
    b->fogEnabled=enable;b->fogColor=color;b->fogChanges++;
    if(b->fogChanges<=12){
        char line[150];
        snprintf(line,sizeof(line),
          "NATIVE_GXM=fog enabled=%d color=%06x mode=tracked_gxp_pending",
          (int)enable,color&0xffffff);
        bs_trace(line);
    }
}
static int bs_native_shader_tag(Renderer*r,int32_t id) {
    if(!r||!r->dataWin||id<0||(uint32_t)id>=r->dataWin->shdr.count)
        return 0;
    Shader*shader=&r->dataWin->shdr.shaders[id];
    if(!shader->present||!shader->name)return 0;
    if(!strcmp(shader->name,"shd_grayscale"))return 1;
    if(!strcmp(shader->name,"shd_invert"))return 2;
    if(!strcmp(shader->name,"shd_grayscale_fade"))return 3;
    if(!strcmp(shader->name,"shd_saturation"))return 4;
    if(!strcmp(shader->name,"shd_alpha"))return 5;
    if(!strcmp(shader->name,"shd_brightness_to_alpha"))return 6;
    if(!strcmp(shader->name,"shd_forcefullalpha"))return 7;
    if(!strcmp(shader->name,"shd_shadowblend"))return 8;
    if(!strcmp(shader->name,"shd_forcecolour"))return 9;
    if(!strcmp(shader->name,"shd_shoujo"))return 10;
    if(!strcmp(shader->name,"shd_lut"))return 11;
    if(!strcmp(shader->name,"shd_castlereflect"))return 12;
    if(!strcmp(shader->name,"shd_castlereflect_layerblend"))return 13;
    /* Chapter 3 board camera: grayscale except the sand palette. */
    if(!strcmp(shader->name,"shd_grayscalesand"))return 14;
    return 0;
}
static void bs_gpuSetShader(Renderer*r,int32_t id){
    BSGxmRenderer*b=BR(r);
    r->currentShader=id;
    /* Report native shader coverage BY ROOM. The old first-20 global-only
     * logging was exhausted in Chapter5's opening, hiding all Castle Town
     * shader requests and making visual-GXP prioritization impossible. */
    int room=r->runner?r->runner->currentRoomIndex:-1;
    if(room!=b->shaderLastRoom){
        b->shaderLastRoom=room;
        memset(b->shaderRoomReported,0,sizeof(b->shaderRoomReported));
    }
    if(id>=0&&id<256&&!b->shaderRoomReported[id]){
        b->shaderRoomReported[id]=1;
        const char*name=(r->dataWin&&(uint32_t)id<r->dataWin->shdr.count&&
                          r->dataWin->shdr.shaders[id].name)?
                         r->dataWin->shdr.shaders[id].name:"<unnamed>";
        char msg[186];snprintf(msg,sizeof(msg),
         "NATIVE_GXM=shader_room room=%d id=%d name=%.90s",
         room,id,name);
        bs_trace(msg);
    }
    extern int g_nativeGxpAvailable;
    int shaderTag=g_nativeGxpAvailable?bs_native_shader_tag(r,id):0;
    if(shaderTag&&!((nvgxmRuntimeShaderMask(b->vg)>>(shaderTag-1))&1u))
        shaderTag=0;
    nvgxmSelectRuntimeShader(b->vg,shaderTag);
    if(shaderTag){
        const float*uniforms=(id>=0&&id<256)?b->shaderValues[id]:NULL;
        nvgxmSetRuntimeShaderParams(b->vg,uniforms);
        if(shaderTag==13||shaderTag==14)
            nvgxmSetRuntimeShaderVectors(b->vg,b->shaderVectors[id]);
        static int logged[15]={0};
        if(logged[shaderTag]++<8){
            char message[165];snprintf(message,sizeof(message),
             "NATIVE_GXM=shader_effect id=%d gxp=%.72s mode=runtime",
             id,r->dataWin->shdr.shaders[id].name);
            bs_trace(message);
        }
        return;
    }
    ++b->shaderFallbacks;
    if(b->shaderFallbacks<=20){
       char line[150];
       snprintf(line,sizeof(line),
          "NATIVE_GXM=shader request=%d backend=fixed_gxm_untranslated count=%u",
          (int)id,b->shaderFallbacks);
       bs_trace(line);
    }
}
static void bs_gpuResetShader(Renderer*r){
    r->currentShader=-1;
    BSGxmRenderer*b=BR(r);
    if(b&&b->vg){
        nvgxmSelectRuntimeShader(b->vg,0);
        nvgxmSetRuntimeShaderParams(b->vg,NULL);
    }
}
static bool bs_shaderIsCompiled(Renderer*r,int32_t id){
    extern int g_nativeGxpAvailable;
    int tag=bs_native_shader_tag(r,id);
    return g_nativeGxpAvailable&&tag>0&&BR(r)->vg&&
      ((nvgxmRuntimeShaderMask(BR(r)->vg)>>(tag-1))&1u);
}
static bool bs_shadersSupported(void){
    extern int g_nativeGxpAvailable;
    return g_nativeGxpAvailable!=0;
}
/* Experimental-GL returns per-program uniform locations and preserves
 * their assigned values across frames. Native GXP uses an asset-scoped
 * opaque handle: ((shaderAsset+1)<<4)|slot, never shared across programs.
 * These five additional effects use at most one scalar uniform each. */
static const char* bs_native_shader_uniform(int tag){
    if(tag==3)return "fade";
    if(tag==4)return "u_desaturation";
    if(tag==5)return "alpha";
    if(tag==8)return "shadowCol";
    if(tag==9)return "forceColour";
    if(tag==10)return "time";
    if(tag==11)return "strength";
    if(tag==12)return "shadowheight";
    if(tag==14)return "sandcol";
    return NULL;
}
static int32_t bs_shaderGetUniform(Renderer*r,int32_t id,char*name){
    extern int g_nativeGxpAvailable;
    if(!g_nativeGxpAvailable||id<0||id>=256||!name||
       !bs_shaderIsCompiled(r,id))return -1;
    int tag=bs_native_shader_tag(r,id);
    const char*expected=bs_native_shader_uniform(tag);
    if(expected&&strcmp(name,expected)==0)return ((id+1)<<4)|1;
    if(tag==10 && strcmp(name,"appSurfWidth")==0)return ((id+1)<<4)|2;
    if(tag==12 && strcmp(name,"pos_y")==0)return ((id+1)<<4)|2;
    if(tag==14){
        if(strcmp(name,"sand1")==0)return ((id+1)<<4)|3;
        if(strcmp(name,"sand2")==0)return ((id+1)<<4)|4;
        if(strcmp(name,"sand3")==0)return ((id+1)<<4)|5;
        if(strcmp(name,"sand4")==0)return ((id+1)<<4)|6;
    }
    if(tag==13){
        if(strcmp(name,"u_object_surface_uvs")==0)return ((id+1)<<4)|3;
        if(strcmp(name,"u_tile_surface_uvs")==0)return ((id+1)<<4)|4;
        if(strcmp(name,"u_mask_surface_uvs")==0)return ((id+1)<<4)|5;
        if(strcmp(name,"u_bounds")==0)return ((id+1)<<4)|6;
    }
    return -1;
}
static int32_t bs_shaderGetSamplerIndex(Renderer*r,int32_t id,char*name){
    if(!name||!bs_shaderIsCompiled(r,id))return -1;
    int tag=bs_native_shader_tag(r,id);
    if(tag==10){
        if(strcmp(name,"texGradient")==0)return 1;
        if(strcmp(name,"texBubble")==0)return 2;
        if(strcmp(name,"texStars")==0)return 3;
    }
    if(tag==11 && strcmp(name,"lut_tex")==0)return 1;
    if(tag==13){
        if(strcmp(name,"u_object_surface")==0)return 1;
        if(strcmp(name,"u_mask_surface")==0)return 2;
    }
    return -1;
}
static void bs_shader_uniform_write(Renderer*r,int32_t handle,
                                    const float*values,unsigned count){
    BSGxmRenderer*b=BR(r);
    if(!b||!values||count==0)return;
    int slot=handle&15;
    if(slot<1||slot>6)return;
    int asset=(handle>>4)-1;
    if(asset<0||asset>=256)return;
    int tag=bs_native_shader_tag(r,asset);
    if(tag==12||tag==13){
        static unsigned logReflectWrites=0;
        if(logReflectWrites++<32){
            char trace[190];
            snprintf(trace,sizeof(trace),
              "NATIVE_GXM=reflect_uniform asset=%d tag=%d slot=%d count=%u "
              "v0=%.4f v1=%.4f v2=%.4f v3=%.4f",
              asset,tag,slot,count,values[0],
              count>1?values[1]:0.0f,
              count>2?values[2]:0.0f,count>3?values[3]:0.0f);
            bs_trace(trace);
        }
    }
    if((tag==13||tag==14) && slot>=3 &&slot<=6){
        if(count>4)count=4;
        for(unsigned i=0;i<count;i++)
            if(isfinite(values[i]))b->shaderVectors[asset][(slot-3)*4+i]=values[i];
        b->shaderUniformWrites++;
        if(r->currentShader==asset&&b->vg)
            nvgxmSetRuntimeShaderVectors(b->vg,b->shaderVectors[asset]);
        return;
    }
    if(slot>2||!bs_native_shader_uniform(tag))return;
    if(slot==2){
        if(tag!=10 && tag!=12)return;
        if(isfinite(values[0]))b->shaderValues[asset][1]=values[0];
    }else{
        if(count>4)count=4;
        for(unsigned i=0;i<count;i++)
            if(isfinite(values[i]))b->shaderValues[asset][i]=values[i];
    }
    b->shaderUniformWrites++;
    if(r->currentShader==asset&&b->vg)
        nvgxmSetRuntimeShaderParams(b->vg,b->shaderValues[asset]);
}
static void bs_shaderSetUniformF(Renderer*r,int32_t id,int32_t count,
    float a,float b,float c,float d){
    float v[4]={a,b,c,d};
    bs_shader_uniform_write(r,id,v,count>0?(unsigned)count:0);
}
static void bs_shaderSetUniformFArray(Renderer*r,int32_t id,float*values,uint32_t count){
    bs_shader_uniform_write(r,id,values,count);
}
static void bs_shaderSetUniformI(Renderer*r,int32_t id,int32_t count,
    int32_t a,int32_t b,int32_t c,int32_t d){
    float v[4]={(float)a,(float)b,(float)c,(float)d};
    bs_shader_uniform_write(r,id,v,count>0?(unsigned)count:0);
}
static void bs_setMatrix(Renderer*r,int32_t type,Matrix4f matrix){
    if(type<0 || type>=MATRICES_MAX) return;
    r->gmlMatrices[type]=matrix;
    if(type==MATRIX_VIEW || type==MATRIX_PROJECTION)
        bs_soft_unsupported("custom_matrix_transform");
}

/* Font sprites are rasterized by the original GameMaker font atlas + glyph metrics. */
static FontGlyph* bs_glyph(Font* font,unsigned ch) {
    if(ch<128 && font->glyphLUT[ch])return font->glyphLUT[ch];
    for(unsigned i=0;i<font->glyphCount;i++)
        if(font->glyphs[i].character==ch)return &font->glyphs[i];
    return NULL;
}
static int bs_utf8(const char **s){
    const unsigned char *p=(const unsigned char*)*s;
    if(!*p)return 0;
    int value=0,extra=0;
    if(p[0]<128){(*s)++;return p[0];}
    if((p[0]&0xE0)==0xC0){value=p[0]&31;extra=1;}
    else if((p[0]&0xF0)==0xE0){value=p[0]&15;extra=2;}
    else {(*s)++;return '?';}
    p++;
    for(int j=0;j<extra;j++){
        if((*p&0xC0)!=0x80){*s=(const char*)p;return '?';}
        value=(value<<6)|(*p++&63);
    }
    *s=(const char*)p;return value;
}
static uint32_t bs_mix_colors(uint32_t a,uint32_t b,float t){
 t=bs_clamp01(t);uint32_t out=0;
 for(unsigned shift=0;shift<24;shift+=8) {
  float av=(float)((a>>shift)&255u),bv=(float)((b>>shift)&255u);
  out|=((uint32_t)(av+(bv-av)*t+0.5f))<<shift;
 }
 return out;
}
static uint32_t bs_gradient_color(uint32_t a,uint32_t b,
 uint32_t c,uint32_t d,float u,float v){
 return bs_mix_colors(bs_mix_colors(a,b,u),bs_mix_colors(d,c,u),v);
}
static void bs_text(Renderer *r,const char* text,float x,float y,float xs,float ys,
       float angle,float separation,uint32_t color,float alpha,
        uint32_t c2,uint32_t c3,uint32_t c4,int gradient) {
    BSGxmRenderer*b=BR(r);DataWin*dw=r->dataWin;
    int index=r->drawFont;
    if(!text || index<0 || (uint32_t)index>=dw->font.count)return;
    /* Chapter0's version is assembled at runtime by its original scripts;
     * the existing Create-event assignment does not override later string
     * construction. Correct only this visible native GXM label, leaving
     * the user-provided DataWin and all other text untouched. */
    extern int g_vitaActiveChapter;
    if(g_vitaActiveChapter==0 &&
       strncmp(text,"DELTARUNE VITA v0.",18)==0){
        /* Base version of this build ("v0.79" from "v0.79-1"). */
        static char label[48];
        if(!label[0]){
            const char*v=NATIVE_GXM_PROBE_VERSION;
            size_t n=strcspn(v,"-");
            snprintf(label,sizeof(label),"DELTARUNE VITA %.*s",(int)n,v);
        }
        text=label;
    }
    Font* f=&dw->font.fonts[index];
    if(!f->present)return;
    Sprite *spriteFont=NULL;
    int img=0;
    if(f->isSpriteFont){
        if(f->spriteIndex<0||(uint32_t)f->spriteIndex>=dw->sprt.count)return;
        spriteFont=&dw->sprt.sprites[f->spriteIndex];
        if(!spriteFont->present||!spriteFont->tpagIndices)return;
        static unsigned spriteFontLogged=0;
        if(spriteFontLogged++<8){
            char line[192];snprintf(line,sizeof(line),
                "NATIVE_GXM=sprite_font font=%d sprite=%d frames=%u glyphs=%u",
                index,f->spriteIndex,spriteFont->textureCount,f->glyphCount);
            bs_trace(line);
        }
    }else{
        if(f->tpagIndex<0 || (uint32_t)f->tpagIndex>=b->imageCount)return;
        img=bs_ensure_image(b,f->tpagIndex);
        if(!img){b->missing++;return;}
    }
    /* Same line layout as the GL renderers (gl_legacy_renderer.c
     * glDrawText): draw_text_ext's separation REPLACES the line stride, \r\n
     * is one break, the font scale applies, and lines start above the
     * baseline by ascenderOffset. Adding separation to the glyph height
     * pushed the next line far below in translated menus. */
    const float fontScaleX=f->scaleX!=0.0f?f->scaleX:1.0f;
    const float fontScaleY=f->scaleY!=0.0f?f->scaleY:1.0f;
    const int32_t textLen=(int32_t)strlen(text);
    float rowHeight=separation<0.0f?TextUtils_lineStride(f):separation/fontScaleY;
    float totalHeight=(float)TextUtils_countLines(text,textLen)*rowHeight;
    nvgSave(b->vg);nvgTranslate(b->vg,x,y);nvgRotate(b->vg,-angle*BS_PI/180);
    nvgScale(b->vg,xs*fontScaleX,ys*fontScaleY);
    if(r->drawValign==1)nvgTranslate(b->vg,0,-totalHeight*.5f);
    else if(r->drawValign==2)nvgTranslate(b->vg,0,-totalHeight);
    const float cyStart=-(float)f->ascenderOffset;
    const char *line=text;float cy=cyStart;
    while(*line){
        const char *end=line;
        while(*end&&!TextUtils_isNewlineChar(*end))end++;
        float lineW=0;
        const char *p=line;
        while(p<end){int ch=bs_utf8(&p);FontGlyph*g=bs_glyph(f,(unsigned)ch);
            if(g)lineW+=g->shift;else lineW+=f->emSize*.35f;}
        float cx=r->drawHalign==1?-lineW*.5f:r->drawHalign==2?-lineW:0;
        float lineStart=cx;
        p=line;
        while(p<end){
            int ch=bs_utf8(&p);FontGlyph*g=bs_glyph(f,(unsigned)ch);
            if(!g){cx+=f->emSize*.35f;continue;}
            float u=lineW>0?(cx-lineStart+g->shift*.5f)/lineW:0;
            float v=totalHeight>0?(cy-cyStart+g->sourceHeight*.5f)/totalHeight:0;
            uint32_t tint=gradient?bs_gradient_color(color,c2,c3,c4,u,v):color;
            if(g->sourceWidth&&g->sourceHeight) {
                if(spriteFont) {
                    /* GameMaker font_add_sprite uses one sprite frame per
                     * glyph. The glyph's array POSITION determines the
                     * frame, not the Unicode codepoint or sourceX/Y. */
                    ptrdiff_t frame=g-f->glyphs;
                    if(frame>=0 && (uint32_t)frame<spriteFont->textureCount) {
                        int ti=spriteFont->tpagIndices[frame];
                        if(ti>=0 && (uint32_t)ti<b->imageCount &&
                           dw->tpag.items[ti].present){
                            TexturePageItem* tp=&dw->tpag.items[ti];
                            int glyphImage=bs_ensure_image(b,ti);
                            if(glyphImage>0) {
                                float px=(uint32_t)ti<b->originalTpagCount?tp->sourceX:0;
                                float py=(uint32_t)ti<b->originalTpagCount?tp->sourceY:0;
                                b->spriteFontGlyphs++;
                                bs_image_tinted(b,glyphImage,b->width[ti],b->height[ti],
                                  px,py,tp->sourceWidth,tp->sourceHeight,
                                  cx+(float)g->offset,
                                  cy+(float)tp->targetY-f->spriteOriginYAdjust,
                                  g->sourceWidth,g->sourceHeight,tint,alpha);
                            } else b->missing++;
                        }
                    }
                } else {
                    TexturePageItem* fontPage=&dw->tpag.items[f->tpagIndex];
                    float px=(uint32_t)f->tpagIndex<b->originalTpagCount?fontPage->sourceX:0;
                    float py=(uint32_t)f->tpagIndex<b->originalTpagCount?fontPage->sourceY:0;
                    bs_image_tinted(b,img,b->width[f->tpagIndex],b->height[f->tpagIndex],
                          g->sourceX+px,g->sourceY+py,g->sourceWidth,g->sourceHeight,
                          cx+g->offset,cy,g->sourceWidth,g->sourceHeight,tint,alpha);
                }
            }
            cx+=g->shift;
        }
        if(!*end)break;
        line=text+TextUtils_skipNewline(text,(int32_t)(end-text),textLen);
        cy+=rowHeight;
    }
    nvgRestore(b->vg);
}
static void bs_drawText(Renderer*r,const char*s,float x,float y,float xs,float ys,
     float angle,float separation){
    bs_text(r,s,x,y,xs,ys,angle,separation,r->drawColor,r->drawAlpha,0,0,0,0);
}
static void bs_drawTextColor(Renderer*r,const char*s,float x,float y,float xs,float ys,
  float angle,int32_t c1,int32_t c2,int32_t c3,int32_t c4,float a,float separation){
    if(c1!=c2||c2!=c3||c3!=c4){
        static int once=0;
        if(!once++){
            bs_trace("NATIVE_GXM=text_gradient backend=bilinear_per_glyph fallback=1");
        }
    }
    bs_text(r,s,x,y,xs,ys,angle,separation,(uint32_t)c1,a,
          (uint32_t)c2,(uint32_t)c3,(uint32_t)c4,c1!=c2||c2!=c3||c3!=c4);
}
static void bs_drawSpriteTiled(Renderer*r,int32_t idx,float ox,float oy,
   float x,float y,float xs,float ys,bool tx,bool ty,float rw,float rh,uint32_t color,float a){
    BSGxmRenderer*b=BR(r);
    if(idx<0||(uint32_t)idx>=b->imageCount)return;
    TexturePageItem *p=&r->dataWin->tpag.items[idx];
    if(!xs||!ys)return;
    float tw=fabsf(p->boundingWidth*xs),th=fabsf(p->boundingHeight*ys);
    if(tw<1||th<1)return;
    nvgSave(b->vg);nvgScissor(b->vg,0,0,rw,rh);
    float maxX=tx?rw:x+tw,maxY=ty?rh:y+th;
    BSGxmVisible vis=bs_visible_local(b);
    /* Room sprite source may extend beyond its bounding tile because of
     * origin/trim; include that full extent in the conservative padding. */
    float padX=fabsf(p->sourceWidth*xs)+fabsf(ox*xs)+2.0f*tw+3.0f;
    float padY=fabsf(p->sourceHeight*ys)+fabsf(oy*ys)+2.0f*th+3.0f;
    int x0=0,x1=0,y0=0,y1=0,total=0,fullX=0,fullY=0;
    bs_tile_index_span(x,tw,maxX,vis.valid?vis.minX-padX:-INFINITY,
                       vis.valid?vis.maxX+padX:INFINITY,&x0,&x1);
    bs_tile_index_span(y,th,maxY,vis.valid?vis.minY-padY:-INFINITY,
                       vis.valid?vis.maxY+padY:INFINITY,&y0,&y1);
    bs_tile_index_span(x,tw,maxX,-INFINITY,INFINITY,&total,&fullX);
    bs_tile_index_span(y,th,maxY,-INFINITY,INFINITY,&total,&fullY);
    b->tiledCalls++;
    b->tileIterationsAvoided+=(uint64_t)fullX*fullY-
                     (uint64_t)(x1-x0)*(y1-y0);
    for(int iy=y0;iy<y1;iy++)for(int ix=x0;ix<x1;ix++){
        bs_drawSprite(r,idx,x+ix*tw,y+iy*th,ox,oy,xs,ys,0,color,a);
        b->tileIterationsExecuted++;
    }
    nvgRestore(b->vg);
}
static void bs_drawTiledPart(Renderer*r,int32_t idx,int32_t sx,int32_t sy,
    int32_t sw,int32_t sh,float x,float y,float w,float h,uint32_t tint,float alpha){
    BSGxmRenderer*b=BR(r);
    if(idx<0||(uint32_t)idx>=b->imageCount||sw<=0||sh<=0)return;
    nvgSave(b->vg);nvgScissor(b->vg,x,y,w,h);
    BSGxmVisible vis=bs_visible_local(b);
    int x0=0,x1=0,y0=0,y1=0,total=0,fullX=0,fullY=0;
    bs_tile_index_span(x,(float)sw,x+w,
        vis.valid?vis.minX-sw:-INFINITY,vis.valid?vis.maxX+sw:INFINITY,&x0,&x1);
    bs_tile_index_span(y,(float)sh,y+h,
        vis.valid?vis.minY-sh:-INFINITY,vis.valid?vis.maxY+sh:INFINITY,&y0,&y1);
    bs_tile_index_span(x,(float)sw,x+w,-INFINITY,INFINITY,&total,&fullX);
    bs_tile_index_span(y,(float)sh,y+h,-INFINITY,INFINITY,&total,&fullY);
    b->tiledCalls++;
    b->tileIterationsAvoided+=(uint64_t)fullX*fullY-
                    (uint64_t)(x1-x0)*(y1-y0);
    for(int iy=y0;iy<y1;iy++)for(int ix=x0;ix<x1;ix++){
        bs_tpag_region(b,idx,sx,sy,sw,sh,
                       x+ix*sw,y+iy*sh,sw,sh,0,0,0,alpha,tint);
        b->tileIterationsExecuted++;
    }
    nvgRestore(b->vg);
}
static void bs_drawTile(Renderer*r,RoomTile* tile,float ox,float oy){
    if(!tile)return;
    if(tile->useSpriteDefinition){
        int idx=Renderer_resolveTPAGIndex(r->dataWin,tile->backgroundDefinition,0);
        if(idx>=0)bs_drawSpritePart(r,idx,tile->sourceX,tile->sourceY,
          tile->width,tile->height,tile->x+ox,tile->y+oy,tile->scaleX,tile->scaleY,
          0,tile->x+ox,tile->y+oy,tile->color,tile->alpha);
    } else {
        int bg=tile->backgroundDefinition;
        if(bg<0||(uint32_t)bg>=r->dataWin->bgnd.count)return;
        int idx=r->dataWin->bgnd.backgrounds[bg].tpagIndex;
        if(idx>=0)bs_drawSpritePart(r,idx,tile->sourceX,tile->sourceY,
          tile->width,tile->height,tile->x+ox,tile->y+oy,tile->scaleX,tile->scaleY,
          0,tile->x+ox,tile->y+oy,tile->color,tile->alpha);
    }
}
/* GameMaker primitives and vertex buffers, decomposed into triangles.
 * Untextured triangles keep their three vertex colours through the native
 * gradient path. A textured triangle maps UV to screen with the exact affine
 * transform of its three vertices (an affine map is exact for a triangle),
 * tinted by the average vertex colour. Lines and points keep vertex colours.
 * Unsupported shapes used to abort the process (C2-12828-1 in the Chapter 3
 * videogame minigame: primitive_vertex_color). */
static void bs_prim_push(BSGxmRenderer*b,float x,float y,uint32_t color,
                         float alpha,float u,float v){
    if(b->primCount>=b->primCap){
        unsigned cap=b->primCap?b->primCap*2u:256u;
        if(cap>1u<<20)return;
        BSGxmVertex*grown=(BSGxmVertex*)realloc(b->primVerts,cap*sizeof(BSGxmVertex));
        if(!grown)return;
        b->primVerts=grown;b->primCap=cap;
    }
    b->primVerts[b->primCount++]=(BSGxmVertex){x,y,u,v,alpha,color&0xFFFFFFu};
}
static uint32_t bs_avg_color3(uint32_t a,uint32_t b,uint32_t c){
    unsigned r=((a&255u)+(b&255u)+(c&255u))/3u;
    unsigned g=(((a>>8)&255u)+((b>>8)&255u)+((c>>8)&255u))/3u;
    unsigned bl=(((a>>16)&255u)+((b>>16)&255u)+((c>>16)&255u))/3u;
    return r|(g<<8)|(bl<<16);
}
static void bs_prim_triangle(Renderer*r,const BSGxmVertex*v0,const BSGxmVertex*v1,
                             const BSGxmVertex*v2){
    BSGxmRenderer*b=BR(r);
    float alpha=(v0->alpha+v1->alpha+v2->alpha)/3.0f;
    if(b->primImage<=0){
        bs_drawTriangle(r,v0->x,v0->y,v1->x,v1->y,v2->x,v2->y,
                        v0->color,v1->color,v2->color,alpha,false);
        return;
    }
    if(alpha<=0||!bs_alpha_uniform_visible(b,alpha))return;
    float du1=v1->u-v0->u,dv1=v1->v-v0->v,du2=v2->u-v0->u,dv2=v2->v-v0->v;
    float det=du1*dv2-du2*dv1;
    uint32_t tint=bs_avg_color3(v0->color,v1->color,v2->color);
    if(!isfinite(det)||fabsf(det)<1e-12f){
        /* Degenerate UVs sample one texel; keep the geometry visible. */
        bs_drawTriangle(r,v0->x,v0->y,v1->x,v1->y,v2->x,v2->y,tint,tint,tint,alpha,false);
        return;
    }
    float dx1=v1->x-v0->x,dx2=v2->x-v0->x,dy1=v1->y-v0->y,dy2=v2->y-v0->y;
    NVGpaint paint=nvgImagePattern(b->vg,0,0,1,1,0,b->primImage,1.0f);
    paint.xform[0]=(dx1*dv2-dx2*dv1)/det;
    paint.xform[1]=(dy1*dv2-dy2*dv1)/det;
    paint.xform[2]=(dx2*du1-dx1*du2)/det;
    paint.xform[3]=(dy2*du1-dy1*du2)/det;
    paint.xform[4]=v0->x-paint.xform[0]*v0->u-paint.xform[2]*v0->v;
    paint.xform[5]=v0->y-paint.xform[1]*v0->u-paint.xform[3]*v0->v;
    paint.extent[0]=paint.extent[1]=1.0f;
    paint.innerColor=paint.outerColor=bs_col(tint,alpha);
    nvgBeginPath(b->vg);
    nvgMoveTo(b->vg,v0->x,v0->y);nvgLineTo(b->vg,v1->x,v1->y);
    nvgLineTo(b->vg,v2->x,v2->y);nvgClosePath(b->vg);
    nvgFillPaint(b->vg,paint);nvgFill(b->vg);
    b->draws++;
}
static void bs_prim_flush(Renderer*r,int32_t type){
    BSGxmRenderer*b=BR(r);
    const BSGxmVertex*v=b->primVerts;
    unsigned n=b->primCount;
    switch(type){
    case PRIMITIVE_POINTS:
        for(unsigned i=0;i<n;i++)
            bs_drawRectangle(r,v[i].x,v[i].y,v[i].x+1,v[i].y+1,v[i].color,v[i].alpha,false);
        break;
    case PRIMITIVE_LINES:
        for(unsigned i=0;i+1<n;i+=2)
            bs_drawLineColor(r,v[i].x,v[i].y,v[i+1].x,v[i+1].y,1,
                             v[i].color,v[i+1].color,(v[i].alpha+v[i+1].alpha)*.5f);
        break;
    case PRIMITIVE_LINE_STRIP:
        for(unsigned i=0;i+1<n;i++)
            bs_drawLineColor(r,v[i].x,v[i].y,v[i+1].x,v[i+1].y,1,
                             v[i].color,v[i+1].color,(v[i].alpha+v[i+1].alpha)*.5f);
        break;
    case PRIMITIVE_TRIANGLES:
        for(unsigned i=0;i+2<n;i+=3)bs_prim_triangle(r,&v[i],&v[i+1],&v[i+2]);
        break;
    case PRIMITIVE_TRIANGLE_STRIP:
        for(unsigned i=0;i+2<n;i++)bs_prim_triangle(r,&v[i],&v[i+1],&v[i+2]);
        break;
    case PRIMITIVE_TRIANGLE_FAN:
        for(unsigned i=1;i+1<n;i++)bs_prim_triangle(r,&v[0],&v[i],&v[i+1]);
        break;
    default:{
        static unsigned logged=0;
        if(logged++<8){
            char line[100];
            snprintf(line,sizeof(line),"NATIVE_GXM=primitive_unknown type=%d vertices=%u",
                     (int)type,n);
            bs_trace(line);
        }
    }}
    b->primCount=0;
}
static void bs_prim_bind_texture(BSGxmRenderer*b,int32_t texture){
    b->primImage=0;b->primW=b->primH=0;
    if(texture==-1)return;
    int image=0,w=0,h=0;
    if(bs_resolve_texture(b,(uint32_t)texture,&image,&w,&h)&&image>0){
        b->primImage=image;b->primW=w;b->primH=h;
    }
}
static void bs_primitiveBegin(Renderer*r,int32_t type){
    BSGxmRenderer*b=BR(r);
    b->primitiveType=type;b->primitiveStarted=1;b->primCount=0;
    b->primImage=0;
}
static void bs_primitiveBeginTexture(Renderer*r,int32_t type,int32_t texture){
    bs_primitiveBegin(r,type);
    bs_prim_bind_texture(BR(r),texture);
}
static void bs_drawVertex(Renderer*r,float x,float y,float z,uint32_t color,
                          float alpha,float u,float v){
    BSGxmRenderer*b=BR(r);
    (void)z;
    if(!b->primitiveStarted)return;
    bs_prim_push(b,x,y,color,alpha,u,v);
}
static void bs_primitiveEnd(Renderer*r){
    BSGxmRenderer*b=BR(r);
    if(b->primitiveStarted)bs_prim_flush(r,b->primitiveType);
    b->primitiveStarted=0;b->primCount=0;b->primImage=0;
}
static float bs_vb_float(const uint8_t*p){float f;memcpy(&f,p,sizeof(f));return f;}
static void bs_drawVertexBuffer(Renderer*r,VertexBuffer*buff,int32_t prim,
  int32_t tex,int32_t offset,int32_t count){
    BSGxmRenderer*b=BR(r);
    if(!buff||!buff->format||!buff->data||buff->format->stride==0)return;
    const VertexFormat*fmt=buff->format;
    int pos=-1,col=-1,uv=-1;
    for(int i=0;i<fmt->numElements;i++){
        const VertexElement*e=&fmt->elements[i];
        if(e->usage==VERTEX_USAGE_POSITION&&pos<0)pos=i;
        else if(e->usage==VERTEX_USAGE_COLOR&&col<0)col=i;
        else if(e->usage==VERTEX_USAGE_TEXCOORD&&uv<0)uv=i;
    }
    if(pos<0)return;
    int32_t total=(int32_t)(buff->size/fmt->stride);
    if(offset<0)offset=0;
    if(offset>total)offset=total;
    if(count<0||count>total-offset)count=total-offset;
    b->primCount=0;
    if(uv>=0)bs_prim_bind_texture(b,tex);else b->primImage=0;
    for(int32_t i=offset;i<offset+count;i++){
        const uint8_t*vx=buff->data+(size_t)i*fmt->stride;
        const VertexElement*pe=&fmt->elements[pos];
        float x=bs_vb_float(vx+pe->offset),y=bs_vb_float(vx+pe->offset+4);
        uint32_t color=0xFFFFFFu;float alpha=1.0f;
        if(col>=0){
            const uint8_t*c=vx+fmt->elements[col].offset;
            color=(uint32_t)c[0]|((uint32_t)c[1]<<8)|((uint32_t)c[2]<<16);
            alpha=c[3]/255.0f;
        }
        float u=0,v=0;
        if(uv>=0){
            const uint8_t*t=vx+fmt->elements[uv].offset;
            u=bs_vb_float(t);v=bs_vb_float(t+4);
        }
        bs_prim_push(b,x,y,color,alpha,u,v);
    }
    bs_prim_flush(r,prim);
    b->primImage=0;
}
static float bs_quad_orient(float ax,float ay,float bx,float by,float cx,float cy){
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
static int bs_quad_edges_cross(float ax,float ay,float bx,float by,
                               float cx,float cy,float dx,float dy){
    float a=bs_quad_orient(ax,ay,bx,by,cx,cy);
    float b=bs_quad_orient(ax,ay,bx,by,dx,dy);
    float c=bs_quad_orient(cx,cy,dx,dy,ax,ay);
    float d=bs_quad_orient(cx,cy,dx,dy,bx,by);
    return ((a>0&&b<0)||(a<0&&b>0))&&((c>0&&d<0)||(c<0&&d>0));
}
static void bs_drawSpritePos(Renderer*r,int32_t idx,float x1,float y1,float x2,float y2,
    float x3,float y3,float x4,float y4,float alpha){
    BSGxmRenderer*b=BR(r);
    if(idx<0||(unsigned)idx>=b->imageCount)return;
    TexturePageItem *tp=&r->dataWin->tpag.items[idx];
    if(!tp->sourceWidth||!tp->sourceHeight)return;
    /* A warped quad is normal GameMaker sprite_pos geometry, not a
     * fatal renderer error. Follow GLRenderer::glDrawSpritePos using two
     * UV-interpolated triangles; reject only degenerate/nonfinite data. */
    const float xy[8]={x1,y1,x2,y2,x3,y3,x4,y4};
    for(int i=0;i<8;i++)if(!isfinite(xy[i]))return;
    if(!isfinite(alpha)||alpha<=0.0f)return;
    /* A transient corrupt/self-intersecting sprite_pos used to be submitted
     * as two unrelated triangles and appeared as a black wedge for one frame
     * in Castle Town. A regular perspective/rotated quad never crosses its
     * opposite edges, so reject only the bow-tie case. */
    if(bs_quad_edges_cross(x1,y1,x2,y2,x3,y3,x4,y4)||
       bs_quad_edges_cross(x2,y2,x3,y3,x4,y4,x1,y1)){
        static unsigned crossedReported=0;
        if(crossedReported++<8)
            bs_trace("NATIVE_GXM=sprite_pos rejected=self_intersecting_quad");
        return;
    }
    float crossA=(x2-x1)*(y3-y1)-(y2-y1)*(x3-x1);
    float crossB=(x3-x1)*(y4-y1)-(y3-y1)*(x4-x1);
    if(fabsf(crossA)<=0.00001f && fabsf(crossB)<=0.00001f)return;
    /* Check all four corners BEFORE texture upload; no visible pixel is
     * rejected for camera rotation, shearing or negative scaling. */
    float t[6],xmin=0,xmax=0,ymin=0,ymax=0;
    nvgCurrentTransform(b->vg,t);
    if(isfinite(t[0])&&isfinite(t[1])&&isfinite(t[2])&&
       isfinite(t[3])&&isfinite(t[4])&&isfinite(t[5])){
        for(int i=0;i<4;i++){
            float px,py;nvgTransformPoint(&px,&py,t,xy[2*i],xy[2*i+1]);
            if(!isfinite(px)||!isfinite(py))return;
            if(i==0){xmin=xmax=px;ymin=ymax=py;}
            else {
                if(px<xmin)xmin=px;if(px>xmax)xmax=px;
                if(py<ymin)ymin=py;if(py>ymax)ymax=py;
            }
        }
        int tw=DISPLAY_WIDTH,th=DISPLAY_HEIGHT;
        if(b->activeTarget>=0&&bs_surface_valid(b,b->activeTarget)){
            tw=b->surfaces[b->activeTarget].w;
            th=b->surfaces[b->activeTarget].h;
        }
        if(xmax< -2||xmin>tw+2||ymax< -2||ymin>th+2){
            b->offscreenCulls++;return;
        }
    }
    int img=bs_ensure_image(b,idx);
    if(!img){b->missing++;return;}
    float iw=(float)b->width[idx],ih=(float)b->height[idx];
    if(iw<=0||ih<=0)return;
    float u0=((unsigned)idx<b->originalTpagCount?tp->sourceX:0)/iw;
    float v0=((unsigned)idx<b->originalTpagCount?tp->sourceY:0)/ih;
    float u1=u0+tp->sourceWidth/iw,v1=v0+tp->sourceHeight/ih;
    const float uv[8]={u0,v0,u1,v0,u1,v1,u0,v1};
    nvgDrawImageQuad(b->vg,img,xy,uv,bs_clamp01(alpha));
    b->draws++;
}

unsigned long BSContractRenderer_lastFrameDraws(Renderer* r) {
    return r?BR(r)->draws:0;
}
void BSContractRenderer_frameStart(Renderer* r) { BSGxmRenderer *b=BR(r); b->frameIsActive=1; b->activeTarget=-1; b->viewActive=b->guiActive=0; }

void BSContractRenderer_metrics(Renderer *r, char *out, unsigned cap) {
    if(!r || !out || cap==0)return;
    BSGxmRenderer *b=BR(r);
    int appId=r->runner?r->runner->applicationSurfaceId:-1;
    BSGxmSurface *app=bs_surface(b,appId);
    unsigned loaded=0,liveSurfaces=0;
    for(unsigned i=0;i<b->pageCount;i++)if(b->pageImages[i]>0)++loaded;
    for(unsigned i=b->originalTpagCount;i<b->imageCount;i++)
        if(b->images[i]>0)++loaded;
    for(int i=0;i<BS_MAX_SURFACES;i++)if(b->surfaces[i].live)++liveSurfaces;
    snprintf(out,cap,
      "NATIVE_GXM=render_metrics room=%d app=%d app_valid=%d app_size=%dx%d active_target=%d world_draws=%lu total_draws=%lu missing=%lu tex_loaded=%u surfaces=%u dynamic=%u snapshots=%lu switches=%lu pages_decoded=%lu items_uploaded=%lu partial_masks=%lu alpha_test=%d alpha_ref=%u alpha_changes=%u cache_mib=%llu peak_mib=%llu bc3=%u rgba4444=%u rgba8888=%u evictions=%u fog_calls=%u shader_fallback=%u offscreen_culls=%lu spritefont_glyphs=%lu tile_skipped=%llu tile_drawn=%llu tiled_calls=%llu retire_pending=%u retire_total=%u shader_uniform_writes=%u r444_cache_hits=%lu r444_cache_absent=%lu r444_cache_rejected=%lu page_load_ms=%llu",
      r->runner?r->runner->currentRoomIndex:-1,
      appId,app!=NULL,app?app->w:0,app?app->h:0,b->activeTarget,
      b->worldDraws,b->draws,b->missing,loaded,
      liveSurfaces,b->dynamicSprites,b->snapshotCount,b->targetSwitches,
      b->pageDecodes,b->itemUploads,b->maskFallbacks,
       b->alphaEnable,b->alphaTestRef,b->alphaTestChanges,
       (unsigned long long)(b->residentBytes/1048576u),
       (unsigned long long)(b->peakResidentBytes/1048576u),
       b->bc3Loads,b->rgba4Loads,b->rgba8Loads,b->cacheEvictions,
       b->fogChanges,b->shaderFallbacks,b->offscreenCulls,b->spriteFontGlyphs,
       (unsigned long long)b->tileIterationsAvoided,
       (unsigned long long)b->tileIterationsExecuted,
       (unsigned long long)b->tiledCalls,
       b->retiredCount,b->retiredTotal,b->shaderUniformWrites,
       b->cacheHits,b->cacheAbsent,b->cacheRejected,
       (unsigned long long)(b->pageLoadUs/1000u));
}
