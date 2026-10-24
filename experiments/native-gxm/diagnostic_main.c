/*
 * Deltarune Vita native-GXM audit-only executable.
 * Reads EVERY Chapter 0-5 without running GML game scenes or mutating saves.
 * It cannot intercept hardware data aborts; durable stage checkpoints
 * identify the last failed chapter and permit skip/resume on next launch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "data_win.h"
#include "vm.h"
#include "nanovg.h"
#include "native_bc3_swizzle.h"

int g_vitaActiveChapter=0, g_vitaProbeLoggingEnabled=1;
int g_nativeStretchFill=0, g_nativeBorderMode=0, g_nativeBetterBorders=0;
NVGcontext *g_nativeSettingsVG=NULL;
#ifndef SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE
#define SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE ((int)0x805B10FF)
#endif
#define NANOVG_GXM_IMPLEMENTATION
#define NANOVG_GXM_UTILS_IMPLEMENTATION
#include "nanovg_gxm_native.h"

#define AUDIT_DIR "ux0:data/deltarune/deltarunevita/gxm-audit-a3"
#define AUDIT_DATA "ux0:data/deltarune/deltarunevita/chapter"
#define AUDIT_MAGIC 0x47415841u
#define AUDIT_VERSION 3u
typedef struct {
    uint32_t magic, version;
    int next, busy, completed, errors, runs;
} AuditState;
static FILE* detail=NULL;
static void make_dirs(void) {
    sceIoMkdir("ux0:data/deltarune",0777);
    sceIoMkdir("ux0:data/deltarune/deltarunevita",0777);
    sceIoMkdir(AUDIT_DIR,0777);
}
static void record(const char* fmt,...) {
    if(!detail)return;
    va_list ap;va_start(ap,fmt);vfprintf(detail,fmt,ap);va_end(ap);
    fputc('\n',detail);
}
static void checkpoint(const char* stage,int chapter) {
    record("AUDIT chapter=%d stage=%s time_us=%llu",
        chapter,stage,(unsigned long long)sceKernelGetProcessTimeWide());
    if(detail)fflush(detail);
}
static void summary(const char* fmt,...) {
    char path[200];snprintf(path,sizeof(path),AUDIT_DIR "/summary.log");
    FILE* f=fopen(path,"ab");if(!f)return;
    va_list ap;va_start(ap,fmt);vfprintf(f,fmt,ap);va_end(ap);
    fputc('\n',f);fclose(f);
}
static AuditState state_load(void) {
    AuditState s={AUDIT_MAGIC,AUDIT_VERSION,0,-1,0,0,0};
    FILE* f=fopen(AUDIT_DIR "/progress.bin","rb");
    if(f) {
        AuditState saved={0};
        if(fread(&saved,1,sizeof(saved),f)==sizeof(saved) &&
           saved.magic==AUDIT_MAGIC&&saved.version==AUDIT_VERSION&&
           saved.next>=0&&saved.next<=6&&
           saved.busy>=-1&&saved.busy<=5) s=saved;
        fclose(f);
    }
    return s;
}
static int state_save(const AuditState* s) {
    FILE* f=fopen(AUDIT_DIR "/progress.tmp","wb");
    if(!f)return 0;
    int ok=fwrite(s,1,sizeof(*s),f)==sizeof(*s);
    if(fflush(f))ok=0;
    fclose(f);
    if(!ok)return 0;
    sceIoRemove(AUDIT_DIR "/progress.bin");
    return sceIoRename(AUDIT_DIR "/progress.tmp",AUDIT_DIR "/progress.bin")>=0;
}
static void rectangle(NVGcontext* vg,float x,float y,float w,float h,NVGcolor color){
    nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillColor(vg,color);nvgFill(vg);
}
static const uint8_t digits[10][7]={
 {31,17,17,17,17,17,31},{4,12,4,4,4,4,14},
 {31,1,1,31,16,16,31},{31,1,1,15,1,1,31},
 {17,17,17,31,1,1,1},{31,16,16,31,1,1,31},
 {31,16,16,31,17,17,31},{31,1,2,4,8,8,8},
 {31,17,17,31,17,17,31},{31,17,17,31,1,1,31}};
static void digit(NVGcontext* vg,int d,float x,float y,float scale) {
    if(d<0||d>9)return;
    for(int row=0;row<7;row++)
      for(int c=0;c<5;c++)
        if(digits[d][row]&(1u<<(4-c)))
            rectangle(vg,x+c*scale,y+row*scale,scale,scale,nvgRGBA(255,255,255,235));
}
static void screen(NVGXMwindow* win,NVGcontext* vg,
                   int chapter,int completed,int status) {
    if(!win||!vg)return;
    gxmBeginFrame();gxmClearColor(.025f,.032f,.052f,1);gxmClear();
    nvgBeginFrame(vg,DISPLAY_WIDTH,DISPLAY_HEIGHT,1);
    rectangle(vg,40,58,880,8,nvgRGB(65,142,83));
    digit(vg,0,76,105,7);
    digit(vg,6,140,105,7);
    if(chapter>=0&&chapter<=5)digit(vg,chapter,445,110,17);
    for(int i=0;i<6;i++) {
        NVGcolor c=i<completed?nvgRGB(54,178,94):
                   i==chapter?(status<0?nvgRGB(205,70,70):nvgRGB(242,193,66)):
                   nvgRGB(61,71,89);
        rectangle(vg,80+i*138,365,115,60,c);
        digit(vg,i,119+i*138,375,4);
    }
    rectangle(vg,70,475,(float)completed/6*820,12,nvgRGB(54,178,94));
    nvgEndFrame(vg);gxmEndFrame();gxmSwapBuffer();
}
static uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t le64(const unsigned char* p) {
    return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32);
}
typedef struct {
    int valid, missing, broken;
    unsigned gpuImage;
    unsigned gpuW,gpuH;
    uint64_t compressedBytes;
} PvrAudit;
static PvrAudit inspect_pvr(int chapter,DataWin* dw,NVGcontext* vg,
                            const uint32_t* maxX,const uint32_t* maxY,
                            const uint32_t* references) {
    PvrAudit result={0};
    for(uint32_t i=0;i<dw->txtr.count;i++){
        char path[230];
        snprintf(path,sizeof(path),AUDIT_DATA "%d/pvr/page_%03u.bc3.pvr",chapter,i);
        FILE* f=fopen(path,"rb");
        if(!f){
            result.missing++;
            record("PVR page=%u status=missing referenced_tpags=%u "
                   "required_atlas=%ux%u action=REBUILD_PVR_FROM_ORIGINAL_TXTR",
                   i,references?references[i]:0,maxX?maxX[i]:0,maxY?maxY[i]:0);
            continue;
        }
        unsigned char h[52];size_t bytes=fread(h,1,52,f);
        if(bytes!=52){result.broken++;record("PVR page=%u status=truncated_header",i);fclose(f);continue;}
        unsigned w=le32(h+28),ht=le32(h+24),meta=le32(h+48);
        uint64_t payload=((uint64_t)(w+3)/4)*((uint64_t)(ht+3)/4)*16;
        const Texture* src=&dw->txtr.textures[i];
        int dimensions=(src->textureWidth<=0||(unsigned)src->textureWidth==w)&&
                       (src->textureHeight<=0||(unsigned)src->textureHeight==ht);
        int atlasFit=(!maxX||!maxY) ||
                     (w>=maxX[i] && ht>=maxY[i]);
        if(!atlasFit)
            record("PVR page=%u issue=TPAG_OUT_OF_BOUNDS pvr_size=%ux%u "
                   "required=%ux%u references=%u",
                   i,w,ht,maxX[i],maxY[i],references?references[i]:0);
        int valid=atlasFit&&le32(h)==0x03525650&&le32(h+4)==0&&le64(h+8)==11&&
                  le32(h+16)==1&&le32(h+20)==0&&le32(h+32)==1&&
                  le32(h+36)==1&&le32(h+40)==1&&le32(h+44)==1&&
                  w>=4&&ht>=4&&w<=4096&&ht<=4096&&
                  !(w&(w-1))&&!(ht&(ht-1))&&meta<=65536&&dimensions;
        int64_t length=-1;
        if(fseek(f,0,SEEK_END)==0)length=ftell(f);
        valid=valid&&length>=0&&(uint64_t)length==52ull+meta+payload;
        if(valid) {
           result.valid++;
           result.compressedBytes+=payload;
           record("PVR page=%u status=valid format=BC3 width=%u height=%u bytes=%llu "
                  "referenced_tpags=%u required_atlas=%ux%u",
                i,w,ht,(unsigned long long)payload,
                references?references[i]:0,maxX?maxX[i]:0,maxY?maxY[i]:0);
           if(vg && !result.gpuImage && w<=128 && ht<=128 && payload<=65536 &&
              fseek(f,52+(long)meta,SEEK_SET)==0){
               uint8_t* blob=(uint8_t*)malloc((size_t)payload);
               if(blob){
                   if(fread(blob,1,(size_t)payload,f)==payload){
                       uint8_t* tiled=(uint8_t*)malloc((size_t)payload);
                       int id=0;
                       if(tiled && nativeBc3Swizzle(tiled,blob,w,ht))
                          id=nvgCreateImageRGBA(vg,w,ht,
                              NVG_IMAGE_DXT5|NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR,tiled);
                       free(tiled);
                       if(id>0){
                           result.gpuImage=(unsigned)id;result.gpuW=w;result.gpuH=ht;
                           record("PVR page=%u status=gxm_small_sample_swizzled_upload_ok",i);
                       }
                   }
                   free(blob);
               }
           }
        }else{
           result.broken++;
           record("PVR page=%u status=invalid width=%u height=%u file_bytes=%lld "
                  "reason=header_size_or_atlas_bounds",i,w,ht,(long long)length);
        }
        fclose(f);
        if(i%32==0 && detail)fflush(detail);
    }
    return result;
}

static void capability_matrix(int ch,DataWin* dw){
    record("CAPABILITY chapter=%d feature=bc3_texture_upload status=implemented_native_gxm",ch);
    record("CAPABILITY chapter=%d feature=rgba4444_texture_upload status=implemented_native_gxm",ch);
    record("CAPABILITY chapter=%d feature=texture_page_cache status=implemented_native_gxm");
    record("CAPABILITY chapter=%d feature=game_settings_ui status=adapted_not_fully_tested");
    record("CAPABILITY chapter=%d feature=png_borders status=implemented_hardware_validation_pending",ch);
    record("CAPABILITY chapter=%d feature=fog status=fallback_state_only effect_not_reproduced",ch);
    record("CAPABILITY chapter=%d feature=custom_gml_shaders status=NOT_IMPLEMENTED count=%u",ch,dw->shdr.count);
    record("CAPABILITY chapter=%d feature=per_texel_alpha_test status=NOT_IMPLEMENTED",ch);
    record("CAPABILITY chapter=%d feature=vertex_buffer_pipeline status=NOT_IMPLEMENTED",ch);
    record("CAPABILITY chapter=%d feature=projective_vertex_uv status=NOT_IMPLEMENTED",ch);
    record("CAPABILITY chapter=%d feature=advanced_blend_min_max_subtract status=NOT_IMPLEMENTED",ch);
    record("CAPABILITY chapter=%d feature=video_decoder status=NOT_IMPLEMENTED",ch);
    record("CAPABILITY chapter=%d feature=room_scripts status=NOT_EXECUTED_SAFE_STATIC_AUDIT",ch);
    record("CAPABILITY chapter=%d feature=frame_rate status=NOT_MEASURED_NO_GAMEPLAY",ch);
}
static int audit_one(int chapter,NVGXMwindow* win,NVGcontext* vg,AuditState*state){
    char path[180];
    snprintf(path,sizeof(path),AUDIT_DIR "/chapter%d.log",chapter);
    detail=fopen(path,"wb");
    if(!detail){summary("AUDIT chapter=%d status=LOG_OPEN_FAILED",chapter);return -1;}
    record("AUDIT_VERSION=v0.78-gxm-audit3 chapter=%d scan=static_data_gpu_sample");
    record("DISCLAIMER no gameplay scenes, no save modifications, no full shader execution");
    state->busy=chapter;state->next=chapter;
    if(!state_save(state))record("AUDIT warning=checkpoint_write_failed");
    checkpoint("file_preflight",chapter);
    snprintf(path,sizeof(path),AUDIT_DATA "%d/data.win",chapter);
    SceIoStat st;
    if(sceIoGetstat(path,&st)<0){
        record("ERROR stage=data_win status=missing path=%s",path);
        summary("AUDIT chapter=%d status=MISSING_DATA path=%s",chapter,path);
        fclose(detail);detail=NULL;return -2;
    }
    record("DATA path=%s bytes=%llu",path,(unsigned long long)st.st_size);
    DataWinParserOptions o={0};
    o.parseGen8=o.parseOptn=o.parseLang=true;
    o.parseExtn=o.parseSond=o.parseAgrp=true;
    o.parseSprt=o.parseBgnd=o.parsePath=true;
    o.parseScpt=o.parseGlob=o.parseShdr=true;
    o.parseFont=o.parseTmln=o.parseObjt=true;
    o.parseRoom=o.parseTpag=o.parseCode=true;
    o.parseVari=o.parseFunc=o.parseStrg=true;
    o.parseTxtr=o.parseAudo=true;
    o.skipLoadingPreciseMasksForNonPreciseSprites=true;
    o.lazyLoadRooms=true;o.lazyLoadTextures=true;o.lazyLoadAudio=true;
    o.loadType=DATAWINLOADTYPE_LOAD_PER_CHUNK;
    checkpoint("parse_start",chapter);
    uint64_t started=sceKernelGetProcessTimeWide();
    DataWin* dw=DataWin_parse(path,o);
    uint64_t duration=sceKernelGetProcessTimeWide()-started;
    if(!dw) {
        record("ERROR stage=parse status=FAILED elapsed_us=%llu",
               (unsigned long long)duration);
        summary("AUDIT chapter=%d status=PARSE_FAILED elapsed_ms=%llu",
               chapter,(unsigned long long)(duration/1000));
        fclose(detail);detail=NULL;return -3;
    }
    record("PARSE status=PASS elapsed_ms=%llu rooms=%u sprites=%u tpag=%u txtr=%u "
           "shader=%u fonts=%u code=%u objects=%u",
           (unsigned long long)(duration/1000),dw->room.count,
           dw->sprt.count,dw->tpag.count,dw->txtr.count,
           dw->shdr.count,dw->font.count,dw->code.count,dw->objt.count);
    checkpoint("room_scan_start",chapter);
    unsigned roomBad=0,roomsWithPayload=0;
    for(unsigned i=0;i<dw->room.count;i++){
        Room*room=&dw->room.rooms[i];
        if(!room->present)roomBad++;
        if(room->payloadLoaded)roomsWithPayload++;
        record("ROOM id=%u present=%d name=%.110s size=%ux%u speed=%u persistent=%d "
               "objects_loaded=%u tiles_loaded=%u lazy=%d",
               i,room->present,room->name?room->name:"<null>",
               room->width,room->height,room->speed,room->persistent,
               room->gameObjectCount,room->tileCount,!room->payloadLoaded);
        if(i%32==0)fflush(detail);
    }
    checkpoint("shader_scan_start",chapter);
    unsigned shaderGlsl=0,shaderHlsl=0,shaderNeedsGxp=0;
    for(unsigned i=0;i<dw->shdr.count;i++){
        Shader*shader=&dw->shdr.shaders[i];
        int gl=(shader->glsl_Vertex||shader->glsl_Fragment||
                shader->glslES_Vertex||shader->glslES_Fragment)!=0;
        int hlsl=(shader->hlsl9_Vertex||shader->hlsl9_Fragment)!=0;
        shaderGlsl+=gl;shaderHlsl+=hlsl;shaderNeedsGxp++;
        record("SHADER id=%u name=%.100s present=%d type=%u glsl=%d hlsl9=%d "
               "vita_cg_vert_bytes=%u vita_cg_frag_bytes=%u "
               "pssl_vert_bytes=%u pssl_frag_bytes=%u "
               "native_gxp=NOT_IMPLEMENTED action=translate_compile_bind",
               i,shader->name?shader->name:"<null>",shader->present,shader->type,
               gl,hlsl,shader->cgVita_VertexLen,shader->cgVita_PixelLen,
               shader->pssl_VertexLen,shader->pssl_PixelLen);
    }
    checkpoint("tpag_scan_start",chapter);
    unsigned badTpag=0,items=0;
    uint32_t* maxX=(uint32_t*)calloc(dw->txtr.count,sizeof(uint32_t));
    uint32_t* maxY=(uint32_t*)calloc(dw->txtr.count,sizeof(uint32_t));
    uint32_t* references=(uint32_t*)calloc(dw->txtr.count,sizeof(uint32_t));
    int atlasIndexAllocOk=maxX&&maxY&&references;
    if(!atlasIndexAllocOk)
        record("ERROR stage=tpag_bounds status=out_of_memory checks=INCOMPLETE");
    for(unsigned i=0;i<dw->tpag.count;i++){
        TexturePageItem*t=&dw->tpag.items[i];
        if(!t->present)continue;
        items++;
        if(t->texturePageId<0||(unsigned)t->texturePageId>=dw->txtr.count ||
           !t->sourceWidth||!t->sourceHeight){
            badTpag++;
            record("TPAG id=%u issue=bad_source_reference page=%d rect=%u,%u,%u,%u",
                i,(int)t->texturePageId,t->sourceX,t->sourceY,
                t->sourceWidth,t->sourceHeight);
        }else if(atlasIndexAllocOk){
            unsigned page=(unsigned)t->texturePageId;
            uint32_t neededX=(uint32_t)t->sourceX+t->sourceWidth;
            uint32_t neededY=(uint32_t)t->sourceY+t->sourceHeight;
            if(neededX>maxX[page])maxX[page]=neededX;
            if(neededY>maxY[page])maxY[page]=neededY;
            ++references[page];
        }
    }
    checkpoint("pvr_scan_start",chapter);
    PvrAudit pvr=inspect_pvr(chapter,dw,vg,maxX,maxY,references);
    free(maxX);free(maxY);free(references);
    record("TEXTURE_SUMMARY pages=%u bc3_ok=%d missing=%d invalid=%d "
           "payload_mib=%.2f tpag_present=%u tpag_invalid=%u",
           dw->txtr.count,pvr.valid,pvr.missing,pvr.broken,
           (double)pvr.compressedBytes/1048576.0,items,badTpag);
    checkpoint("vm_create_start",chapter);
    started=sceKernelGetProcessTimeWide();
    VMContext* vm=VM_create(dw);
    record("VM status=%s elapsed_ms=%llu",
           vm?"PASS":"FAILED",
           (unsigned long long)((sceKernelGetProcessTimeWide()-started)/1000));
    int vmOk=vm!=NULL;
    if(vm)VM_free(vm);
    checkpoint("gpu_sample_start",chapter);
    unsigned gpuBc3=pvr.gpuImage>0;
    uint16_t packed[32*32];
    for(int i=0;i<32*32;i++)packed[i]=(uint16_t)(((i/32/4)&1)?0xf0ff:0xff2f);
    int rgba4=vg?nvgCreateImageRGBA(vg,32,32,
                NVG_IMAGE_RGBA4444|NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR,(uint8_t*)packed):0;
    record("GPU_TEST mode=sample_only note=allocation_success_NOT_pixel_color_correctness "
           "gxm_context=%d bc3_small_upload=%s "
           "rgba4444_upload=%s",
           vg!=NULL,vg?(gpuBc3?"PASS":"NO_SUITABLE_PVR_OR_ALLOC_FAILED"):"SKIP_NO_GXM",
           vg?(rgba4>0?"PASS":"ALLOC_FAILED"):"SKIP_NO_GXM");
    if(vg&&win){
        gxmBeginFrame();gxmClearColor(.03f,.04f,.06f,1);gxmClear();
        nvgBeginFrame(vg,DISPLAY_WIDTH,DISPLAY_HEIGHT,1);
        if(pvr.gpuImage){
            nvgBeginPath(vg);nvgRect(vg,85,88,200,200);
            nvgFillPaint(vg,nvgImagePattern(vg,85,88,200,200,0,(int)pvr.gpuImage,1));
            nvgFill(vg);
        }
        if(rgba4>0){
            nvgBeginPath(vg);nvgRect(vg,360,88,200,200);
            nvgFillPaint(vg,nvgImagePattern(vg,360,88,200,200,0,rgba4,1));
            nvgFill(vg);
        }
        nvgEndFrame(vg);gxmEndFrame();gxmSwapBuffer();
        sceGxmFinish(win->context);
        if(pvr.gpuImage)nvgDeleteImage(vg,(int)pvr.gpuImage);
        if(rgba4>0)nvgDeleteImage(vg,rgba4);
    }
    capability_matrix(chapter,dw);
    int problems=(int)(roomBad+badTpag+pvr.broken+pvr.missing)+
                 (vmOk?0:1)+(atlasIndexAllocOk?0:1);
    record("AUDIT chapter=%d status=%s problems=%d missing_bc3=%d shader_gxp_pending=%u "
           "room_payloads_not_executed=%u",
           chapter,problems?"WARN":"PASS_STATIC",problems,pvr.missing,
           shaderNeedsGxp,dw->room.count-roomsWithPayload);
    checkpoint("free_data_start",chapter);
    unsigned roomCount=dw->room.count;
    DataWin_free(dw);
    checkpoint("chapter_completed",chapter);
    fclose(detail);detail=NULL;
    summary("AUDIT chapter=%d status=%s room=%u shaders=%u "
            "glsl=%u hlsl9=%u bc3=%d missing=%d broken=%d "
            "gpu_bc3_sample=%d gpu_rgba4444_sample=%d tpags_bad=%u",
            chapter,problems?"WARN":"PASS_STATIC",roomCount,
            shaderNeedsGxp,shaderGlsl,shaderHlsl,
            pvr.valid,pvr.missing,pvr.broken,gpuBc3,rgba4>0,badTpag);
    return problems?1:0;
}
int main(void) {
    make_dirs();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);
    int reset=(pad.buttons&(SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER))==
              (SCE_CTRL_LTRIGGER|SCE_CTRL_RTRIGGER);
    AuditState state=state_load();
    if(reset){state=(AuditState){AUDIT_MAGIC,AUDIT_VERSION,0,-1,0,0,state.runs+1};}
    if(state.busy>=0){
        summary("AUDIT chapter=%d status=INTERRUPTED previous_stage_in_chapter_log=1 "
                "reason=power_loss_or_native_exception_or_forced_exit action=skip_to_next_on_resume",
                state.busy);
        state.errors++;
        state.next=state.busy+1;
        state.busy=-1;
        state_save(&state);
    }
    if(state.next==0 && state.completed==0){
        FILE*f=fopen(AUDIT_DIR "/summary.log","wb");
        if(f){
            fprintf(f,"GXM_AUDIT=v0.78-audit3 mode=static_all_chapters_gpu_sample\n");
            fprintf(f,"LIMITS no GML room simulation, no custom GXP execution, no promise of zero native faults\n");
            fclose(f);
        }
    }
    NVGXMinitOptions o={.msaa=SCE_GXM_MULTISAMPLE_NONE,.swapInterval=1,
                        .dumpShader=0,.scenesPerFrame=1};
    NVGXMwindow* win=gxmCreateWindow(&o);
    NVGcontext* vg=win?nvgCreateGXM(win->context,win->shader_patcher,0):NULL;
    if(!win||!vg)
        summary("AUDIT warning=GXM_INIT_FAILED continue=metadata_only");
    for(int ch=state.next;ch<=5;ch++){
        screen(win,vg,ch,state.completed,0);
        int result=audit_one(ch,win,vg,&state);
        if(result!=0)state.errors++;
        state.next=ch+1;state.busy=-1;state.completed=ch+1;
        state_save(&state);
        screen(win,vg,ch,state.completed,result<0?-1:1);
        sceKernelDelayThread(150000);
    }
    summary("AUDIT result=FINISHED chapters_scanned=%d errors=%d "
            "details=chapter0.log..chapter5.log",state.completed,state.errors);
    state_save(&state);
    for(int i=0;i<150;i++){
        sceCtrlPeekBufferPositive(0,&pad,1);
        if(pad.buttons&SCE_CTRL_START)break;
        screen(win,vg,-1,state.completed,1);
        sceKernelDelayThread(50000);
    }
    if(vg){sceGxmFinish(win->context);nvgDeleteGXM(vg);}
    if(win)gxmDeleteWindow(win);
    sceKernelExitProcess(0);
    return 0;
}
