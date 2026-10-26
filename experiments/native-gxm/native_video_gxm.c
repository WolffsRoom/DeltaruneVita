/* Native-GXM-only SceAvPlayer video adapter (no VitaGL, no ShaRK).
 * This experimental path shares the existing GameMaker video builtins and
 * exposes decoded YUV frames as a GXM sampler through NanoVG's native handle.
 * It does not mix MP4 audio; Chapter5's standalone OGG remains with OpenAL.
 */
#include "native_video_gxm.h"
#include "nanovg_gxm_native.h"
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>
#include <psp2/io/stat.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#define AV_MAX_BLOCKS 32
typedef struct{void*address;SceUID uid;unsigned size;}AVBlock;
static AVBlock blocks[AV_MAX_BLOCKS];
static volatile int blocksLock=0;
static SceAvPlayerHandle player=0;
static NVGcontext* vg=NULL;
static SceGxmTexture texture;
static int image=0,status=0,event=0,initialized=0,loop=0,closeRequested=0;
static float volume=1.0f,duration=40874.167f,position=0;
static uint32_t videoW=640,videoH=480,videoFrames=0;
static uint64_t startUs=0,lastFetchUs=0,lastProgressUs=0;
static SceUID audioThread=-1;
static volatile int audioThreadRunning=0;
static int audioPort=-1;
static uint32_t audioFrames=0;
static char source[320];
static void avLog(const char*msg,int code){
  char line[512];snprintf(line,sizeof(line),
    "NATIVE_GXM=video step=%s code=0x%08X frame=%u size=%ux%u path=%.160s\n",
    msg,(unsigned)code,videoFrames,videoW,videoH,source);
  FILE*f=fopen("ux0:data/deltarune/deltarunevita/native-gxm-probe.log","ab");
  if(f){fwrite(line,1,strlen(line),f);fclose(f);}
}
static void lockBlocks(void){
    while(__sync_lock_test_and_set(&blocksLock,1))sceKernelDelayThread(50);
}
static void unlockBlocks(void){__sync_lock_release(&blocksLock);}
static void* avCpuAlloc(void*u,uint32_t align,uint32_t bytes){
    (void)u;
    if(align<16)align=16;
    if(align&(align-1))align=32;
    return memalign(align,bytes);
}
static void avCpuFree(void*u,void*p){(void)u;free(p);}
static void* avGpuAlloc(void*u,uint32_t align,uint32_t bytes){
    (void)u;
    if(align<0x40000)align=0x40000;
    uint32_t size=(bytes+0xFFFFFu)&~0xFFFFFu;
    if(!size||size>0x4000000u)return NULL;
    SceUID uid=sceKernelAllocMemBlock("native_gxm_video",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,size,NULL);
    if(uid<0){avLog("gpu_alloc_failed",uid);return NULL;}
    void* ptr=NULL;
    if(sceKernelGetMemBlockBase(uid,&ptr)<0||!ptr||
       sceGxmMapMemory(ptr,size,SCE_GXM_MEMORY_ATTRIB_RW)<0){
        sceKernelFreeMemBlock(uid);avLog("gpu_map_failed",-1);return NULL;
    }
    lockBlocks();
    int slot=-1;
    for(int i=0;i<AV_MAX_BLOCKS;i++)
        if(!blocks[i].address){slot=i;break;}
    if(slot>=0)blocks[slot]=(AVBlock){ptr,uid,size};
    unlockBlocks();
    if(slot<0){
        sceGxmUnmapMemory(ptr);sceKernelFreeMemBlock(uid);
        avLog("gpu_blocks_exhausted",-1);return NULL;
    }
    return ptr;
}
static void avGpuFree(void*u,void*p){
    (void)u;
    if(!p)return;
    lockBlocks();
    AVBlock found={0};
    for(int i=0;i<AV_MAX_BLOCKS;i++)
        if(blocks[i].address==p){found=blocks[i];blocks[i]=(AVBlock){0};break;}
    unlockBlocks();
    if(found.address){
        sceGxmUnmapMemory(found.address);
        sceKernelFreeMemBlock(found.uid);
    }
}
static int avAudioThread(SceSize args,void*arg){
    (void)args;(void)arg;
    int logged=0;
    while(audioThreadRunning){
        if(!player||status==0){sceKernelDelayThread(2000);continue;}
        if(status==3){sceKernelDelayThread(4000);continue;}
        SceAvPlayerFrameInfo frame;memset(&frame,0,sizeof(frame));
        if(sceAvPlayerGetAudioData(player,&frame)&&frame.pData&&
           frame.details.audio.sampleRate>0){
            int rate=frame.details.audio.sampleRate;
            int channels=frame.details.audio.channelCount==1?1:2;
            SceAudioOutMode mode=channels==1?
                SCE_AUDIO_OUT_MODE_MONO:SCE_AUDIO_OUT_MODE_STEREO;
            if(audioPort<0){
                audioPort=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                                   1024,rate,mode);
                if(audioPort<0)
                    audioPort=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE,
                                                   1024,rate,mode);
                avLog(audioPort>=0?"audio_port_open":"audio_port_unavailable",
                     audioPort);
            }
            if(audioPort>=0){
                int volumeInt=(int)(volume*SCE_AUDIO_VOLUME_0DB);
                int vols[2]={volumeInt,volumeInt};
                sceAudioOutSetVolume(audioPort,
                    SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,vols);
                int result=sceAudioOutOutput(audioPort,frame.pData);
                audioFrames++;
                if(!logged++){avLog(result>=0?"audio_first_output":
                                     "audio_output_failed",result);}
            }
        }else sceKernelDelayThread(4000);
    }
    if(audioPort>=0){sceAudioOutReleasePort(audioPort);audioPort=-1;}
    return sceKernelExitDeleteThread(0);
}
static void avStopAudio(void){
    audioThreadRunning=0;
    if(audioThread>=0){
        sceKernelWaitThreadEnd(audioThread,NULL,NULL);
        sceKernelDeleteThread(audioThread);
        audioThread=-1;
    }
}
static void avGpuCleanup(void){
    lockBlocks();
    AVBlock remaining[AV_MAX_BLOCKS];
    memcpy(remaining,blocks,sizeof(blocks));
    memset(blocks,0,sizeof(blocks));
    unlockBlocks();
    for(int i=0;i<AV_MAX_BLOCKS;i++)if(remaining[i].address){
        sceGxmUnmapMemory(remaining[i].address);
        sceKernelFreeMemBlock(remaining[i].uid);
    }
}
static int validPlayer(SceAvPlayerHandle h){
    uint32_t p=(uint32_t)h;
    return p>=0x81000000u&&p<0xA0000000u&&!(p&3u);
}
static int pathExists(const char*p){
    SceIoStat st;
    return p&&sceIoGetstat(p,&st)>=0;
}
static const char* resolveVideo(const char*in,char*out,size_t cap){
    if(pathExists(in))return in;
    const char*name=in?strrchr(in,'/'):NULL;
    name=name?name+1:in;
    if(!name||!*name)return NULL;
    const int chapters[]={3,5,0};
    const char*roots[]={
        "ux0:data/deltarune/deltarunevita/vid/%s",
        "ux0:data/deltarune/deltarunevita/chapter3/vid/%s",
        "ux0:data/deltarune/deltarunevita/chapter5/vid/%s",
        "ux0:data/deltarune/deltarunevita/%s"
    };
    (void)chapters;
    for(unsigned i=0;i<sizeof(roots)/sizeof(roots[0]);i++){
        snprintf(out,cap,roots[i],name);
        if(pathExists(out))return out;
    }
    return NULL;
}
int VitaVideo_init(void){
    if(initialized)return 0;
    int r=sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
    if(r<0&&r!=(int)0x80540101){avLog("sysmodule_failed",r);return r;}
    initialized=1;avLog("sysmodule_ready",r);return 0;
}
void VitaVideo_attachRenderer(NVGcontext*context){vg=context;}
/* A GameMaker video_close() may execute in Runner_step while a GXM scene
 * and NanoVG frame are open. sceGxmFinish there can deadlock waiting for
 * end-scene. Retire SceAvPlayer ONLY after the host swaps the completed frame.
 */
static void avPerformClose(void){
    if(!player){closeRequested=0;return;}
    avStopAudio();
    NVGXMwindow*win=gxmGetWindow();
    if(win)sceGxmFinish(win->context);
    if(image>0&&vg){nvgDeleteImage(vg,image);image=0;}
    int s=sceAvPlayerStop(player);
    int c=sceAvPlayerClose(player);
    avLog("close",s<0?s:c);
    player=0;status=0;closeRequested=0;
    avGpuCleanup();
}
void VitaVideo_close(void){
    if(!player){status=0;return;}
    status=0;closeRequested=1;
}
void VitaVideo_afterPresent(void){
    if(closeRequested)avPerformClose();
}
void VitaVideo_shutdown(void){
    if(player){closeRequested=1;avPerformClose();}
    if(initialized)sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
    initialized=0;
}
int VitaVideo_open(const char*path){
    if(player) {
        VitaVideo_close();
        avLog("reopen_waits_for_frame_boundary",-1);
        event=2;
        return -1;
    }
    event=0;videoFrames=0;
    char full[320]={0};
    const char*resolved=resolveVideo(path,full,sizeof(full));
    if(!resolved){snprintf(source,sizeof(source),"%.290s",path?path:"");avLog("path_missing",-1);event=2;return -1;}
    snprintf(source,sizeof(source),"%s",resolved);
    if(VitaVideo_init()<0){event=2;return -1;}
    SceAvPlayerInitData init;memset(&init,0,sizeof(init));
    init.memoryReplacement.allocate=avCpuAlloc;
    init.memoryReplacement.deallocate=avCpuFree;
    init.memoryReplacement.allocateTexture=avGpuAlloc;
    init.memoryReplacement.deallocateTexture=avGpuFree;
    init.basePriority=0xA0;init.autoStart=1;
    /* VitaGL's validated cold-boot setup (vita_video.c VIDEO_BUFFERS): with
     * only two buffers the decoder rewrote the frame the GPU was still
     * sampling, so parts of the 960x544 intro flickered. Chapter videos keep
     * the two-buffer producer/consumer setup, as on VitaGL. */
    init.numOutputVideoFrameBuffers=strstr(source,"first_load")?5:2;
    player=sceAvPlayerInit(&init);
    if(!validPlayer(player)){
        avLog("player_init_failed",(int)player);
        player=0;avGpuCleanup();event=2;return -1;
    }
    int r=sceAvPlayerAddSource(player,source);
    if(r<0){avLog("add_source_failed",r);VitaVideo_close();event=2;return r;}
    sceAvPlayerSetLooping(player,loop);
    duration=strstr(source,"ch5_intro")?30333.333f:40874.167f;
    position=0;startUs=sceKernelGetProcessTimeWide();
    lastFetchUs=0;lastProgressUs=startUs;
    status=1;
    audioFrames=0;audioThreadRunning=1;
    audioThread=sceKernelCreateThread("GXM_AV_Audio",avAudioThread,
                                      0x10000100,64*1024,0,0,NULL);
    if(audioThread>=0){
        int sr=sceKernelStartThread(audioThread,0,NULL);
        if(sr<0){sceKernelDeleteThread(audioThread);audioThread=-1;
                 audioThreadRunning=0;avLog("audio_thread_start_failed",sr);}
    }else{
        audioThreadRunning=0;avLog("audio_thread_create_failed",audioThread);
    }
    avLog("open_ok",r);
    return 0;
}
bool VitaVideo_updateFrame(void){
    if(!player||!status||closeRequested)return false;
    if(status==3)return true;
    uint64_t now=sceKernelGetProcessTimeWide();
    if(lastFetchUs&&now-lastFetchUs<33000u)return status==2;
    lastFetchUs=now;
    if(sceAvPlayerIsActive(player)){
        SceAvPlayerFrameInfo frame;memset(&frame,0,sizeof(frame));
        if(sceAvPlayerGetVideoData(player,&frame)&&frame.pData&&
            frame.details.video.width>0&&frame.details.video.height>0){
            unsigned w=frame.details.video.width,h=frame.details.video.height;
            if(w<=2048&&h<=2048&&vg){
                int r=sceGxmTextureInitLinear(&texture,frame.pData,
                    SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,w,h,0);
                if(r==0){
                    sceGxmTextureSetMinFilter(&texture,SCE_GXM_TEXTURE_FILTER_LINEAR);
                    sceGxmTextureSetMagFilter(&texture,SCE_GXM_TEXTURE_FILTER_LINEAR);
                    if(image<=0)image=nvgxmCreateImageFromHandle(vg,&texture);
                    else{
                        NVGXMtexture*handle=nvgxmImageHandle(vg,image);
                        if(handle){handle->tex=texture;handle->data=frame.pData;}
                    }
                    videoW=w;videoH=h;videoFrames++;
                    lastProgressUs=now;
                    if(status==1){status=2;event=1;avLog("first_frame",0);}
                    if(videoFrames%120==0)avLog("frame_progress",0);
                    position=(float)frame.timeStamp;
                }else avLog("texture_init_failed",r);
            }
        }
    }else if(status==2){
        avLog("end_of_stream",0);
        VitaVideo_close();event=2;
        return false;
    }
    /* SceAvPlayer may remain active forever if the decoder stalls. Keep
     * GameMaker from freezing permanently on black/caption-only screens.
     * 10s without an advancing VIDEO frame signals video_end recovery. */
    if(status==2 && lastProgressUs && now-lastProgressUs>10000000ull){
        avLog("video_frame_stalled_recover_end",-1);
        VitaVideo_close();event=2;
        return false;
    }
    if(status==1&&now-startUs>5000000ull){
        avLog("prepare_timeout",-1);VitaVideo_close();event=2;return false;
    }
    return status==2;
}
bool VitaVideo_hasDecodedFrame(void){return image>0&&status>0;}
void VitaVideo_draw(float x,float y,float w,float h){(void)x;(void)y;(void)w;(void)h;}
void VitaVideo_drawHost(float x,float y,float w,float h){VitaVideo_draw(x,y,w,h);}
void VitaVideo_drawHostBrightness(float x,float y,float w,float h,float brightness){
    (void)brightness;VitaVideo_draw(x,y,w,h);
}
float VitaVideo_getWidth(void){return (float)videoW;}
float VitaVideo_getHeight(void){return (float)videoH;}
int VitaVideo_isChapter5Intro(void){return strstr(source,"ch5_intro")!=NULL;}
int VitaVideo_shouldApplyAudioResync(void){return 0;}
void VitaVideo_pause(void){if(player&&status==2){sceAvPlayerPause(player);status=3;}}
void VitaVideo_resume(void){if(player&&status==3){sceAvPlayerResume(player);status=2;}}
int VitaVideo_getStatus(void){return status;}
float VitaVideo_getDuration(void){return duration;}
float VitaVideo_getPosition(void){return position;}
int VitaVideo_consumeEvent(void){int e=event;event=0;return e;}
void VitaVideo_setLooping(bool enabled){loop=enabled;if(player)sceAvPlayerSetLooping(player,enabled);}
void VitaVideo_setVolume(float v){volume=v;(void)volume;}
int VitaVideo_image(void){return image;}
