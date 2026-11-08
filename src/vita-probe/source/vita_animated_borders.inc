/* Optional animations: pre-converted raw frames from mods/bordersanim.
 *
 * v0.80-47 playback path (the old one cost ~30 ms of the main thread per
 * animation frame in the v0.80-46 logs):
 *  - sceIoPread and the copy to GPU memory run on a low-priority worker, on
 *    the fourth core when CapUnlocker is installed. The main thread only flips
 *    which texture is bound and never waits for the card: a late frame simply
 *    stays on screen a little longer.
 *  - glTexSubImage2D is never used. For a texture drawn recently VitaGL
 *    allocates new storage and copies the whole old image before applying the
 *    update. Three persistent textures are written directly instead, and the
 *    one being written is never the one on screen or the previous one.
 *  - frames_side.bani holds only the two columns the 4:3 game cannot cover, at
 *    native 960x544 and RGB565. Same bytes per frame as the old half-resolution
 *    stream, with no scaling. It needs the game to cover the middle of the
 *    screen, so a zoomed-out display falls back to frames_fast.bani.
 *
 * frames.bani (the 960x544 master) stays in the mod and is not read here. */
#include "vita_thread_policy.h"

#define ANIM_BORDER_ROOT "ux0:data/deltarune/deltarunevita/mods/bordersanim/"
#define ANIM_BORDER_SLOTS 3
#define ANIM_BORDER_HEADER_BYTES 12
/* Columns kept from each side of the screen. A 640x480 game covers 725 of 960
 * columns at 100% zoom, so 118 are exposed on each side; 120 leaves a margin. */
#define ANIM_BORDER_SIDE_COLUMNS 120
#define ANIM_BORDER_SIDE_W (ANIM_BORDER_SIDE_COLUMNS*2)
#define ANIM_BORDER_SIDE_H 544
#define ANIM_BORDER_FAST_W 480
#define ANIM_BORDER_FAST_H 272
#define ANIM_BORDER_MAX_FRAME_BYTES (ANIM_BORDER_SIDE_W*ANIM_BORDER_SIDE_H*2)
#define ANIM_BORDER_PERF_FLIPS 120U

extern int g_vitaDisplayOffsetX;
extern int g_vitaDisplayOffsetY;
extern int g_vitaDisplayZoom;
extern int g_vitaDisplayStretchToFill;

enum { ANIM_JOB_IDLE=0, ANIM_JOB_PENDING, ANIM_JOB_RUNNING, ANIM_JOB_DONE, ANIM_JOB_FAILED };

static GLuint animatedBorderTextures[ANIM_BORDER_SLOTS]={0};
static void* animatedBorderTextureData[ANIM_BORDER_SLOTS]={0};
static unsigned char* animatedBorderStaging=NULL;
static SceUID animatedBorderFd=-1;
static char animatedBorderName[64]={0};
static char animatedBorderFailedName[64]={0};
static char animatedBorderNoSideName[64]={0};
static int animatedBorderCount=0,animatedBorderDelayMs=100;
static int animatedBorderBytes=0,animatedBorderIsSide=0;
static int animatedBorderSlot=0,animatedBorderFrame=-1;
static uint64_t animatedBorderStartUs=0;

/* Worker state. Fields are written before animatedBorderJobState is released,
 * and the worker only touches them between PENDING and DONE/FAILED. */
static SceUID animatedBorderThread=-1,animatedBorderSema=-1;
static volatile int animatedBorderWorkerExit=0;
static int animatedBorderJobState=ANIM_JOB_IDLE;
static int animatedBorderJobFrame=0;
static void* animatedBorderJobTarget=NULL;
/* Written by the worker before DONE, read by the main thread after it. */
static uint32_t animatedBorderPerfReadUs=0,animatedBorderPerfReadMaxUs=0;
static uint32_t animatedBorderPerfCopyUs=0;
static unsigned animatedBorderPerfFlips=0,animatedBorderPerfLate=0;

static int animatedBorderJobGet(void){
    return __atomic_load_n(&animatedBorderJobState,__ATOMIC_ACQUIRE);
}
static void animatedBorderJobSet(int state){
    __atomic_store_n(&animatedBorderJobState,state,__ATOMIC_RELEASE);
}
static int animatedBorderWorker(SceSize args,void* argp){
    (void)args;(void)argp;
    while(!animatedBorderWorkerExit){
        if(sceKernelWaitSema(animatedBorderSema,1,NULL)<0)break;
        if(animatedBorderWorkerExit)break;
        if(animatedBorderJobGet()!=ANIM_JOB_PENDING)continue;
        animatedBorderJobSet(ANIM_JOB_RUNNING);
        uint64_t t0=sceKernelGetProcessTimeWide();
        SceOff offset=ANIM_BORDER_HEADER_BYTES+
            (SceOff)animatedBorderJobFrame*animatedBorderBytes;
        int n=sceIoPread(animatedBorderFd,animatedBorderStaging,
                         animatedBorderBytes,offset);
        uint64_t t1=sceKernelGetProcessTimeWide();
        if(n!=animatedBorderBytes){
            animatedBorderJobSet(ANIM_JOB_FAILED);
            continue;
        }
        memcpy(animatedBorderJobTarget,animatedBorderStaging,
               (size_t)animatedBorderBytes);
        uint64_t t2=sceKernelGetProcessTimeWide();
        uint32_t readUs=(uint32_t)(t1-t0);
        animatedBorderPerfReadUs+=readUs;
        if(readUs>animatedBorderPerfReadMaxUs)animatedBorderPerfReadMaxUs=readUs;
        animatedBorderPerfCopyUs+=(uint32_t)(t2-t1);
        animatedBorderJobSet(ANIM_JOB_DONE);
    }
    return sceKernelExitDeleteThread(0);
}
/* Says which step failed, since a missing mod and a code bug otherwise log alike. */
static const char* animatedBorderFailStage="unknown";
static int animatedBorderFailCode=0;
static void animatedBordersNoteFailure(const char* stage,int code){
    animatedBorderFailStage=stage;
    animatedBorderFailCode=code;
}
static int animatedBordersEnsureWorker(void){
    if(animatedBorderThread>=0)return 1;
    if(animatedBorderSema<0){
        animatedBorderSema=sceKernelCreateSema("Deltarune border sema",0,0,1,NULL);
        if(animatedBorderSema<0){
            animatedBordersNoteFailure("worker_sema",animatedBorderSema);
            return 0;
        }
    }
    bool fourthCore=false;
    animatedBorderWorkerExit=0;
    /* Lower priority than the game and audio threads (larger is lower). The
     * default maps to 160 and the lowest user priority is 191, so an offset above
     * 31 is rejected: the +40 of v0.80-47 stopped every animation from starting. */
    SceUID thread=VitaThread_createBackground("Deltarune border reader",
        animatedBorderWorker,0x10000100+20,0x4000,0,&fourthCore);
    if(thread<0){animatedBordersNoteFailure("worker_create",thread);return 0;}
    int started=sceKernelStartThread(thread,0,NULL);
    if(started<0){
        animatedBordersNoteFailure("worker_start",started);
        sceKernelDeleteThread(thread);
        return 0;
    }
    animatedBorderThread=thread;
    borderLog(fourthCore?"animated_border_worker core=4":"animated_border_worker core=default");
    return 1;
}
static void animatedBordersWaitIdle(void){
    /* The worker always finishes its read, and a stalled card would hang the
     * game's own reads as well. Only reached on an animation change. */
    for(;;){
        int state=animatedBorderJobGet();
        if(state!=ANIM_JOB_PENDING&&state!=ANIM_JOB_RUNNING)break;
        sceKernelDelayThread(500);
    }
    animatedBorderJobSet(ANIM_JOB_IDLE);
}
static void animatedBordersRequest(int frame,void* target){
    animatedBorderJobFrame=frame;
    animatedBorderJobTarget=target;
    animatedBorderJobSet(ANIM_JOB_PENDING);
    sceKernelSignalSema(animatedBorderSema,1);
}
static void animatedBordersLogPerf(void){
    /* Writing to BORDER_LOG on the main thread during VitaBorders_draw stalled
     * overlay_us for 20-43 ms every 120 flips on SD2Vita. Keep the counters in
     * memory and avoid synchronous main-thread file I/O during rendering. */
    (void)animatedBorderPerfReadUs;
    (void)animatedBorderPerfReadMaxUs;
    (void)animatedBorderPerfCopyUs;
    (void)animatedBorderPerfFlips;
    (void)animatedBorderPerfLate;
}
static void animatedBordersResetPerf(void){
    animatedBorderPerfReadUs=animatedBorderPerfReadMaxUs=0;
    animatedBorderPerfCopyUs=0;
    animatedBorderPerfFlips=animatedBorderPerfLate=0;
}
static void animatedBordersStop(void){
    animatedBordersWaitIdle();
    animatedBordersLogPerf();
    animatedBordersResetPerf();
    int hasTextures=0;
    for(int i=0;i<ANIM_BORDER_SLOTS;i++)
        if(animatedBorderTextures[i])hasTextures=1;
    if(hasTextures){
        glBindTexture(GL_TEXTURE_2D,0);
        glFinish(); /* do not release a texture still being referenced by GXM */
        for(int i=0;i<ANIM_BORDER_SLOTS;i++){
            if(animatedBorderTextures[i])glDeleteTextures(1,&animatedBorderTextures[i]);
            animatedBorderTextures[i]=0;
            animatedBorderTextureData[i]=NULL;
        }
    }
    if(animatedBorderFd>=0){sceIoClose(animatedBorderFd);animatedBorderFd=-1;}
    free(animatedBorderStaging);animatedBorderStaging=NULL;
    animatedBorderName[0]='\0';
    animatedBorderCount=0;animatedBorderFrame=-1;animatedBorderStartUs=0;
    animatedBorderSlot=0;animatedBorderBytes=0;animatedBorderIsSide=0;
}
static void animatedBordersShutdownWorker(void){
    animatedBordersStop();
    if(animatedBorderThread>=0){
        SceUInt timeout=1000000;
        animatedBorderWorkerExit=1;
        sceKernelSignalSema(animatedBorderSema,1);
        sceKernelWaitThreadEnd(animatedBorderThread,NULL,&timeout);
        animatedBorderThread=-1;
    }
    if(animatedBorderSema>=0){sceKernelDeleteSema(animatedBorderSema);animatedBorderSema=-1;}
}
/* Opens one stream and validates its header and exact size, so a partial or
 * wrong file falls back to the static border instead of failing mid-playback. */
static int animatedBordersOpenStream(const char* name,int side){
    char path[300];
    snprintf(path,sizeof(path),ANIM_BORDER_ROOT "%s/%s",name,
             side?"frames_side.bani":"frames_fast.bani");
    SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);
    if(fd<0){
        animatedBordersNoteFailure(side?"open_side":"open_fast",fd);
        return -1;
    }
    unsigned char h[ANIM_BORDER_HEADER_BYTES]={0};
    int n=sceIoRead(fd,h,sizeof(h));
    int w=(int)h[4]|((int)h[5]<<8),ht=(int)h[6]|((int)h[7]<<8);
    int total=(int)h[8]|((int)h[9]<<8);
    int ms=(int)h[10]|((int)h[11]<<8);
    int expectedW=side?ANIM_BORDER_SIDE_W:ANIM_BORDER_FAST_W;
    int expectedH=side?ANIM_BORDER_SIDE_H:ANIM_BORDER_FAST_H;
    int bytes=expectedW*expectedH*2;
    if(n!=ANIM_BORDER_HEADER_BYTES||memcmp(h,side?"BANS":"BANI",4)!=0||
       w!=expectedW||ht!=expectedH||total<2||total>120||ms<20||ms>1000||
       sceIoLseek(fd,0,SCE_SEEK_END)!=
           (SceOff)ANIM_BORDER_HEADER_BYTES+(SceOff)total*bytes){
        animatedBordersNoteFailure(side?"invalid_side":"invalid_fast",n);
        sceIoClose(fd);
        return -1;
    }
    animatedBorderFd=fd;
    animatedBorderCount=total;
    animatedBorderDelayMs=ms;
    animatedBorderBytes=bytes;
    animatedBorderIsSide=side;
    return 0;
}
/* Any border whose art has a stream in the mod folder animates, so new packs
 * need no code change: borders/<name>_0.png maps to bordersanim/<name>/. */
static int animatedBordersNameFromPath(const char* selected,char* name,size_t size){
    if(!selected||!selected[0])return 0;
    const char* leaf=strrchr(selected,'/');
    leaf=leaf?leaf+1:selected;
    size_t length=strlen(leaf);
    static const char suffix[]="_0.png";
    static const char pngSuffix[]=".png";
    size_t stem=0;
    if(strncmp(leaf,"border_",7)==0 && length>sizeof(suffix)-1 &&
       strcmp(leaf+length-(sizeof(suffix)-1),suffix)==0)
        stem=length-(sizeof(suffix)-1);
    else if((strcmp(leaf,"border1.png")==0 || strcmp(leaf,"border2.png")==0 ||
             strcmp(leaf,"border3.png")==0) &&
            length>sizeof(pngSuffix)-1)
        stem=length-(sizeof(pngSuffix)-1);
    if(stem==0||stem>=size)return 0;
    memcpy(name,leaf,stem);
    name[stem]='\0';
    return 1;
}
/* True when the game image hides everything between the two side columns.
 * Every chapter runs at 640x480 (see the DISPLAY=runtime log lines). */
static int animatedBordersGameCoversMiddle(void){
    int effW=725*g_vitaDisplayZoom/100,effH=544*g_vitaDisplayZoom/100;
    int startX=(960-effW)/2+g_vitaDisplayOffsetX;
    int startY=(544-effH)/2+g_vitaDisplayOffsetY;
    return startX<=ANIM_BORDER_SIDE_COLUMNS &&
           startX+effW>=960-ANIM_BORDER_SIDE_COLUMNS &&
           startY<=0 && startY+effH>=544;
}
static int animatedBordersHasFast(const char* name){
    static char checked[64]={0};
    static int exists=0;
    if(strcmp(checked,name)!=0){
        char path[256];
        snprintf(path,sizeof(path),"%s%s/frames_fast.bani",ANIM_BORDER_ROOT,name);
        SceIoStat stat;
        exists=sceIoGetstat(path,&stat)>=0&&stat.st_size>0;
        snprintf(checked,sizeof(checked),"%s",name);
    }
    return exists;
}

static int animatedBordersStart(const char* requested,int wantSide){
    int useSide=wantSide&&strcmp(animatedBorderNoSideName,requested)!=0;
    if(useSide&&animatedBordersOpenStream(requested,1)<0){
        snprintf(animatedBorderNoSideName,sizeof(animatedBorderNoSideName),"%s",requested);
        borderLog(strcmp(animatedBorderFailStage,"open_side")==0?
                  "animated_border_side_missing":"animated_border_side_invalid");
        useSide=0;
    }
    if(!useSide&&animatedBordersOpenStream(requested,0)<0)return 0;
    if(!animatedBordersEnsureWorker())return 0;
    animatedBorderStaging=(unsigned char*)malloc((size_t)animatedBorderBytes);
    if(!animatedBorderStaging){
        animatedBordersNoteFailure("staging_alloc",animatedBorderBytes);
        return 0;
    }
    int w=animatedBorderIsSide?ANIM_BORDER_SIDE_W:ANIM_BORDER_FAST_W;
    int h=animatedBorderIsSide?ANIM_BORDER_SIDE_H:ANIM_BORDER_FAST_H;
    /* Row pitch is the width aligned to 8 pixels; both sizes are multiples. */
    GLint oldTex=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTex);
    while(glGetError()!=GL_NO_ERROR){}
    glGenTextures(ANIM_BORDER_SLOTS,animatedBorderTextures);
    for(int i=0;i<ANIM_BORDER_SLOTS;i++){
        glBindTexture(GL_TEXTURE_2D,animatedBorderTextures[i]);
        GLint filter=animatedBorderIsSide?GL_NEAREST:GL_LINEAR; /* side is 1:1 */
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(animatedBorderIsSide)
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,NULL);
        else
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_SHORT_4_4_4_4,NULL);
        animatedBorderTextureData[i]=vglGetTexDataPointer(GL_TEXTURE_2D);
        if(!animatedBorderTextureData[i]||glGetError()!=GL_NO_ERROR){
            animatedBordersNoteFailure("texture_alloc",i);
            glBindTexture(GL_TEXTURE_2D,(GLuint)oldTex);
            return 0;
        }
    }
    glBindTexture(GL_TEXTURE_2D,(GLuint)oldTex);
    /* The first frame is read here so the surround is never empty. Starting an
     * animation already coincides with a border change or a settings change. */
    if(sceIoPread(animatedBorderFd,animatedBorderStaging,animatedBorderBytes,
                  ANIM_BORDER_HEADER_BYTES)!=animatedBorderBytes){
        animatedBordersNoteFailure("first_read",0);
        return 0;
    }
    memcpy(animatedBorderTextureData[0],animatedBorderStaging,(size_t)animatedBorderBytes);
    animatedBorderSlot=0;
    animatedBorderFrame=0;
    animatedBorderStartUs=sceKernelGetProcessTimeWide();
    snprintf(animatedBorderName,sizeof(animatedBorderName),"%s",requested);
    animatedBordersRequest(1%animatedBorderCount,animatedBorderTextureData[1]);
    borderLog(animatedBorderIsSide?"animated_border_initialized mode=side":
                                   "animated_border_initialized mode=fast");
    return 1;
}
static GLuint animatedBordersGetTexture(void){
    /* Stretch-to-fill hides the surround completely, so there is nothing to
     * animate. Keep any resident texture only while it can come back quickly. */
    if(!g_vitaAnimatedBordersEnabled || !g_vitaConsoleBordersEnabled ||
       g_vitaDisplayStretchToFill ||
       borderDrawSuppressed || borderMenuSuppressed || borderSceneSuppressed ||
       borderBlackHold ||
       borderTexture==0 || borderPath[0]=='\0'){
        if(animatedBorderTextures[0] && (!g_vitaAnimatedBordersEnabled ||
           g_vitaDisplayStretchToFill ||
           borderMenuSuppressed || borderSceneSuppressed || borderBlackHold))
            animatedBordersStop();
        if(!g_vitaAnimatedBordersEnabled){
            animatedBorderFailedName[0]='\0';
            animatedBorderNoSideName[0]='\0';
        }
        return 0;
    }
    char requested[64];
    if(!animatedBordersNameFromPath(borderPath,requested,sizeof(requested))){
        if(animatedBorderTextures[0])animatedBordersStop();
        return 0;
    }
    /* A missing or damaged stream must not be retried from the card every
     * frame. It stays on the static border until the player turns the option
     * off and on again, or another border is selected. */
    if(strcmp(animatedBorderFailedName,requested)==0){
        if(animatedBorderTextures[0])animatedBordersStop();
        return 0;
    }
    /* v0.80-58: without frames_fast.bani, keep the side stream even when the
     * middle is exposed; drawBorderCurrent puts the static border under it. */
    /* The side-only stream is tiny and remains safe under atlas pressure.
     * Prefer it instead of disabling the animation completely. */
    int wantSide=borderMemoryPressure || animatedBordersGameCoversMiddle()||
                 !animatedBordersHasFast(requested);
    int running=strcmp(animatedBorderName,requested)==0;
    if(running && animatedBorderIsSide!=wantSide &&
       (animatedBorderIsSide||strcmp(animatedBorderNoSideName,requested)!=0)){
        animatedBordersStop();
        running=0;
    }
    if(!running){
        animatedBordersStop();
        if(!animatedBordersStart(requested,wantSide)){
            animatedBordersStop();
            snprintf(animatedBorderFailedName,sizeof(animatedBorderFailedName),"%s",requested);
            char failure[96];
            snprintf(failure,sizeof(failure),"animated_border_start_failed stage=%s code=0x%08X",
                     animatedBorderFailStage,(unsigned)animatedBorderFailCode);
            borderLog(failure);
            return 0;
        }
    }
    int state=animatedBorderJobGet();
    if(state==ANIM_JOB_FAILED){
        borderLog("animated_border_read_failed");
        animatedBordersStop();
        snprintf(animatedBorderFailedName,sizeof(animatedBorderFailedName),"%s",requested);
        return 0;
    }
    if(state==ANIM_JOB_DONE){
        uint64_t now=sceKernelGetProcessTimeWide();
        int due=(int)(((now-animatedBorderStartUs)/1000ULL/
                      (uint64_t)animatedBorderDelayMs)%(uint64_t)animatedBorderCount);
        if(due!=animatedBorderFrame){
            int ready=animatedBorderJobFrame;
            if(due!=ready)animatedBorderPerfLate++;
            animatedBorderSlot=(animatedBorderSlot+1)%ANIM_BORDER_SLOTS;
            animatedBorderFrame=ready;
            if(++animatedBorderPerfFlips>=ANIM_BORDER_PERF_FLIPS){
                animatedBordersLogPerf();
                animatedBordersResetPerf();
            }
            /* Aim the next read at what will be due next, which also catches
             * up after a long frame instead of playing the backlog. */
            animatedBordersRequest((due+1)%animatedBorderCount,
                animatedBorderTextureData[(animatedBorderSlot+1)%ANIM_BORDER_SLOTS]);
        }
    }
    return animatedBorderTextures[animatedBorderSlot];
}
