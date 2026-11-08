#include <stdint.h>
#include <math.h>
#include <psp2/audioin.h>
#include <psp2/kernel/threadmgr.h>

/* Privacy: PCM exists only in this thread's stack and is never persisted.
 * Input is opened only while needed (Sound calibration or Mike's scenes).
 * The API is blocking, so capture must not run on the render/VM thread. */
static volatile int g_vitaMicRequested=0, g_vitaMicAvailable=0;
static volatile int g_vitaMicRaw=0, g_vitaMicKeepRunning=0;
static SceUID g_vitaMicThread=-1;
static int vita_mic_worker(SceSize args,void*argp){
    (void)args;(void)argp;
    int port=-1;
    int16_t pcm[256];
    while(g_vitaMicKeepRunning){
        if(!g_vitaMicRequested){
            if(port>=0){sceAudioInReleasePort(port);port=-1;}
            g_vitaMicAvailable=0;g_vitaMicRaw=0;
            sceKernelDelayThread(55000);
            continue;
        }
        if(port<0){
            port=sceAudioInOpenPort(SCE_AUDIO_IN_PORT_TYPE_VOICE,256,16000,
                                   SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO);
            if(port<0){
                g_vitaMicAvailable=0;
                sceKernelDelayThread(350000);
                continue;
            }
            g_vitaMicAvailable=1;
        }
        int rc=sceAudioInInput(port,pcm);
        if(rc<0){
            g_vitaMicAvailable=0;g_vitaMicRaw=0;
            sceAudioInReleasePort(port);port=-1;
            sceKernelDelayThread(70000);
            continue;
        }
        uint64_t sum=0;
        for(unsigned i=0;i<256;i++){
            int v=pcm[i];sum+=(uint64_t)(v*v);
        }
        float rms=sqrtf((float)sum/256.0f);
        /* 3500 PCM RMS ~= full scale before user sensitivity. */
        int newLevel=(int)fminf(100.0f,rms*100.0f/3500.0f);
        int prev=g_vitaMicRaw;
        g_vitaMicRaw=(newLevel>prev)?(prev*2+newLevel*3)/5:
                                   (prev*7+newLevel)/8;
    }
    if(port>=0)sceAudioInReleasePort(port);
    g_vitaMicRaw=0;g_vitaMicAvailable=0;
    return 0;
}
void VitaMic_start(void){
    if(g_vitaMicThread>=0)return;
    g_vitaMicKeepRunning=1;
    g_vitaMicThread=sceKernelCreateThread("dvita_mic_capture",
        vita_mic_worker,0x10000100,16*1024,0,0,NULL);
    if(g_vitaMicThread>=0)sceKernelStartThread(g_vitaMicThread,0,NULL);
    else g_vitaMicKeepRunning=0;
}
void VitaMic_request(int enabled){g_vitaMicRequested=enabled?1:0;}
int VitaMic_isAvailable(void){return g_vitaMicAvailable;}
int VitaMic_getLevel(void){return g_vitaMicRaw;}
void VitaMic_stop(void){
    g_vitaMicRequested=0;g_vitaMicKeepRunning=0;
    if(g_vitaMicThread>=0){
        sceKernelWaitThreadEnd(g_vitaMicThread,NULL,NULL);
        sceKernelDeleteThread(g_vitaMicThread);
        g_vitaMicThread=-1;
    }
}
