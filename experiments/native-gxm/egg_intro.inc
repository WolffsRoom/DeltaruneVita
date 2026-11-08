/*
 * Deltarune Vita — Chapter 0 prologue, faithfully using room_man assets
 * extracted from the user's Chapter 1 data.win (no changes to data.win).
 * The scene is separate from the chapter0 selector and all language mods.
 */
#include <AL/al.h>
#include <AL/alc.h>
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY

#include "native_asset_pack.h"
#define EGG_ASSET_ROOT "app0:assets/prologue/"
#define EGG_CH1_ROOT EGG_ASSET_ROOT "ch1_original/"
#include "egg_ch1_text_layout.h"
#define EGG_WORLD_X 160.0f
#define EGG_WORLD_Y 32.0f
#define EGG_WORLD_W 640.0f
#define EGG_WORLD_H 480.0f

static const char *const egg_languages[] = {
    "English", "Portuguese-BR", "Japanese", "Spanish", "French",
    "German", "Italian", "Turkish", "Russian", "Russkiy"
};

/* Read packed prologue directly; loose fallback for development installs. */
static unsigned char*egg_asset_read(const char*path,size_t*length){
    unsigned char*data=NULL;
    if(GxmAssetPack_read(path,&data,length))return data;
    FILE*f=fopen(path,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long n=ftell(f);
    if(n<=0||n>4*1024*1024||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    data=malloc((size_t)n);
    if(!data){fclose(f);return NULL;}
    if(fread(data,1,(size_t)n,f)!=(size_t)n){free(data);fclose(f);return NULL;}
    fclose(f);*length=(size_t)n;return data;
}
static int egg_load_png(NVGcontext *vg, const char *path) {
    size_t n=0;
    unsigned char*bytes=egg_asset_read(path,&n);
    if(!bytes)return 0;
    int w=0,h=0,id=0;
    {
        uint8_t *pixels=ImageDecoder_decodeToRgba(bytes,(size_t)n,false,&w,&h);
        if(pixels&&w>0&&h>0&&w<=2048&&h<=2048)
            id=nvgCreateImageRGBA(vg,w,h,NVG_IMAGE_NEAREST,pixels);
        free(pixels);
    }
    free(bytes);
    return id;
}
static void egg_image(NVGcontext *vg,int image,float x,float y,
                      float w,float h,float alpha){
    if(image<=0)return;
    NVGpaint paint=nvgImagePattern(vg,x,y,w,h,0,image,alpha);
    nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillPaint(vg,paint);nvgFill(vg);
}
static void egg_panel(NVGcontext *vg,float x,float y,float w,float h,
                      unsigned char r,unsigned char g,unsigned char b,unsigned char a){
    nvgBeginPath(vg);nvgRect(vg,x,y,w,h);
    nvgFillColor(vg,nvgRGBA(r,g,b,a));nvgFill(vg);
}

/* Bitmap text uses the user's local 8bitoperator JVE at build time.
 * Each glyph is drawn from its generated atlas with a subtle sine-wave
 * offset and a genuine typewriter reveal; no TTF file is bundled. */
static void egg_draw_text(NVGcontext *vg,int sheet,int lang,int slot,
                          float x,float y,unsigned frame,int visible){
    if(sheet<=0||lang<0||lang>=10||slot<0||slot>=EGG_SLOTS)return;
    unsigned count=egg_glyph_counts[lang][slot];
    if(visible>=0 && (unsigned)visible<count)count=(unsigned)visible;
    for(unsigned i=0;i<count;i++){
        const EggGlyphLocation *g=&egg_glyph_locations[lang][slot][i];
        if(!g->visible || g->w==0)continue;
        float shift=sinf((float)frame*0.13f+(float)i*0.37f)*1.35f;
        float gx=x+g->x, gy=y+g->y+shift;
        NVGpaint paint=nvgImagePattern(vg,x,y+shift-(float)slot*EGG_SLOT_H,
                                      EGG_SHEET_W,EGG_SLOT_H*EGG_SLOTS,
                                      0,sheet,1);
        nvgBeginPath(vg);
        nvgRect(vg,gx,gy,(float)g->w+1.0f,27);
        nvgFillPaint(vg,paint);
        nvgFill(vg);
    }
}
/* Prologue presentation order: language first, then independent pages
 * for thanks, project background, optional trophies and CapUnlocker,
 * a personalized greeting, the choice and its farewell. */
#define EGG_TYPE_FRAMES_PER_GLYPH 3u
static int egg_stage_slot(int stage){
    static const int slots[]={0,3,4,1,7,2,8,5,6,6,10};
    return (stage>=0&&stage<=10)?slots[stage]:0;
}
static void egg_draw_option(NVGcontext *vg,int sheet,int option,int selected,
                            float x,float y,unsigned frame){
    if(sheet<=0)return;
    int row=option+(selected?2:0);
    float dx=selected?sinf(frame*0.42f)*1.6f:0;
    float dy=selected?cosf(frame*0.71f)*1.6f:0;
    x+=dx;y+=dy;
    NVGpaint paint=nvgImagePattern(vg,x,y-(float)row*45,230,45*4,0,sheet,1);
    nvgBeginPath(vg);nvgRect(vg,x,y,230,45);
    nvgFillPaint(vg,paint);nvgFill(vg);
}
static int egg_load_language_graphics(NVGcontext *vg,int lang,
                                      int *sheet,int *icon,int *options,int *prompt,int *langHelp) {
    char path[192];
    snprintf(path,sizeof(path),EGG_ASSET_ROOT "text_jve/%s.png",egg_languages[lang]);
    int text=egg_load_png(vg,path);
    snprintf(path,sizeof(path),EGG_ASSET_ROOT "language_cards/%s.png",egg_languages[lang]);
    int flag=egg_load_png(vg,path);
    snprintf(path,sizeof(path),EGG_ASSET_ROOT "options_jve/%s.png",egg_languages[lang]);
    int optionsImg=egg_load_png(vg,path);
    snprintf(path,sizeof(path),EGG_ASSET_ROOT "prompt_jve/%s.png",egg_languages[lang]);
    int promptImg=egg_load_png(vg,path);
    snprintf(path,sizeof(path),EGG_ASSET_ROOT "language_instructions/%s.png",egg_languages[lang]);
    int helpImg=egg_load_png(vg,path);
    if(text<=0||flag<=0||optionsImg<=0||promptImg<=0||helpImg<=0){
        if(text>0)nvgDeleteImage(vg,text);
        if(flag>0)nvgDeleteImage(vg,flag);
        if(optionsImg>0)nvgDeleteImage(vg,optionsImg);
        if(promptImg>0)nvgDeleteImage(vg,promptImg);
        if(helpImg>0)nvgDeleteImage(vg,helpImg);
        return 0;
    }
    if(*sheet>0)nvgDeleteImage(vg,*sheet);
    if(*icon>0)nvgDeleteImage(vg,*icon);
    if(*options>0)nvgDeleteImage(vg,*options);
    if(*prompt>0)nvgDeleteImage(vg,*prompt);
    if(*langHelp>0)nvgDeleteImage(vg,*langHelp);
    *sheet=text;*icon=flag;*options=optionsImg;*prompt=promptImg;
    *langHelp=helpImg;
    return 1;
}
static int egg_language_index(const char *name) {
    for(unsigned i=0;i<sizeof(egg_languages)/sizeof(egg_languages[0]);++i)
        if(name&&strcmp(name,egg_languages[i])==0)return (int)i;
    return 0;
}
/* Base languages are always installed. Ignore bundled .ini and icon-only
 * entries: require an actual language data.win on the memory card. */
static int egg_language_installed(const VitaSettings* settings,int index){
    if(index==0||index==2)return 1;
    if(!settings||index<0||index>=10)return 0;
    const char*name=egg_languages[index];
    int discovered=0;
    for(int i=0;i<settings->modCount;i++){
        if(strcmp(name,settings->modNames[i])==0){discovered=1;break;}
    }
    if(!discovered)return 0;
    char path[192]; SceIoStat st;
    for(int chapter=0;chapter<=5;chapter++){
        snprintf(path,sizeof(path),
          "ux0:data/deltarune/deltarunevita/mods/Lang/%s/chapter%d/data.win",
          name,chapter);
        if(sceIoGetstat(path,&st)>=0&&SCE_S_ISREG(st.st_mode)&&st.st_size>512)
            return 1;
    }
    snprintf(path,sizeof(path),
       "ux0:data/deltarune/deltarunevita/mods/Lang/%s/data.win",name);
    return sceIoGetstat(path,&st)>=0 && SCE_S_ISREG(st.st_mode) && st.st_size>512;
}
static int egg_available_languages(const VitaSettings* settings,int* indices){
    static const int order[]={0,2,1,3,4,5,6,7,8,9};
    int count=0;
    for(unsigned i=0;i<sizeof(order)/sizeof(order[0]);i++)
        if(egg_language_installed(settings,order[i]))indices[count++]=order[i];
    return count;
}
static int egg_change_language(VitaSettings *settings,NVGcontext *vg,
                               int *index,int delta,int *sheet,
                               int *icon,int *options,int *prompt,int *langHelp,
                               const int*available,int count){
    int pos=0;
    for(int i=0;i<count;i++)if(available[i]==*index){pos=i;break;}
    int next=available[(pos+count+delta)%count];
    if(!egg_load_language_graphics(vg,next,sheet,icon,options,prompt,langHelp))return 0;
    *index=next;
    int applied=VitaSettings_selectIntroLanguage(settings,egg_languages[next]);
    char msg[192];
    snprintf(msg,sizeof(msg),
      "NATIVE_GXM=egg_intro language=%.70s installed=1 immediate=%d options=%d",
      egg_languages[next],applied,count);
    log_line(msg);
    return applied;
}
/* Exact embedded Chapter 1 AUDO PCM16 SFX, exported by the project script. */
static unsigned egg_wave_buffer(const char *filename){
    size_t bytes=0;
    unsigned char*data=egg_asset_read(filename,&bytes);
    if(!data)return 0;
    if(bytes<44||bytes>1024*1024){free(data);return 0;}
    if(memcmp(data,"RIFF",4)||memcmp(data+8,"WAVE",4)){free(data);return 0;}
    unsigned channels=0,rate=0,bits=0;
    const unsigned char *samples=NULL;unsigned size=0;
    for(size_t at=12;at+8<=(size_t)bytes;){
        unsigned len=(unsigned)data[at+4]|((unsigned)data[at+5]<<8)|
            ((unsigned)data[at+6]<<16)|((unsigned)data[at+7]<<24);
        if(at+8+(size_t)len>(size_t)bytes)break;
        if(memcmp(data+at,"fmt ",4)==0 && len>=16){
            channels=data[at+10]|(data[at+11]<<8);
            rate=data[at+12]|(data[at+13]<<8)|
               (data[at+14]<<16)|(data[at+15]<<24);
            bits=data[at+22]|(data[at+23]<<8);
        } else if(memcmp(data+at,"data",4)==0){
            samples=data+at+8;size=len;
        }
        at+=8u+len+(len&1u);
    }
    unsigned buffer=0;
    if(samples&&size&&bits==16&&(channels==1||channels==2)&&rate){
        alGenBuffers(1,&buffer);
        if(buffer)alBufferData(buffer,channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,
                              samples,(ALsizei)size,(ALsizei)rate);
    }
    free(data);
    return buffer;
}
typedef struct EggMusic {
    ALCdevice *device;
    ALCcontext *context;
    stb_vorbis *decoder;
    ALuint bgSource,bgBuffers[3],fxSource,fxBuffers[6];
    ALuint laughSource,laughBuffer;
    int channels,rate,active;
    short scratch[4096*2];
} EggMusic;
static int egg_music_fill(EggMusic *a,ALuint buffer){
    if(!a->decoder||!buffer)return 0;
    int frames=stb_vorbis_get_samples_short_interleaved(
        a->decoder,a->channels,a->scratch,4096*a->channels);
    if(frames<=0){
        stb_vorbis_seek_start(a->decoder);
        frames=stb_vorbis_get_samples_short_interleaved(
             a->decoder,a->channels,a->scratch,4096*a->channels);
    }
    if(frames<=0)return 0;
    alBufferData(buffer,a->channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,
                 a->scratch,frames*a->channels*(int)sizeof(short),a->rate);
    return 1;
}
static void egg_music_init(EggMusic *a){
    memset(a,0,sizeof(*a));
    a->device=alcOpenDevice(NULL);
    if(!a->device){log_line("NATIVE_GXM=egg_ch1 audio_device_unavailable");return;}
    a->context=alcCreateContext(a->device,NULL);
    if(!a->context||!alcMakeContextCurrent(a->context)){
        log_line("NATIVE_GXM=egg_ch1 audio_context_unavailable");return;
    }
    a->active=1;
    alGenSources(1,&a->bgSource);
    alGenSources(1,&a->fxSource);
    alGenSources(1,&a->laughSource);
    alSourcef(a->bgSource,AL_GAIN,0.36f);
    alSourcef(a->fxSource,AL_GAIN,0.47f);
    alSourcef(a->laughSource,AL_GAIN,0.57f);
    static const char *const sfx[]={
      "snd_egg","snd_text","snd_menumove","snd_select",
      "snd_txttor","AUDIO_APPEARANCE"
    };
    for(int i=0;i<6;i++){
        char path[220];
        if(i>=4)snprintf(path,sizeof(path),EGG_ASSET_ROOT "sfx/%s.wav",sfx[i]);
        else snprintf(path,sizeof(path),EGG_CH1_ROOT "sfx/%s.wav",sfx[i]);
        a->fxBuffers[i]=egg_wave_buffer(path);
    }
    /* Decode the original 0.61s Spamton voice once, separate from typing SFX.
     * A packaged fallback allows playback even if user data is incomplete. */
    const char *laughPath="ux0:data/deltarune/deltarunevita/music/spamton_laugh_noise.ogg";
    int channels=0,rate=0;
    short *samples=NULL;
    int frames=stb_vorbis_decode_filename(laughPath,&channels,&rate,&samples);
    if(frames<=0){
        laughPath=EGG_ASSET_ROOT "sfx/spamton_laugh_noise.ogg";
        frames=stb_vorbis_decode_filename(laughPath,&channels,&rate,&samples);
    }
    if(frames>0&&samples&&(channels==1||channels==2)){
        alGenBuffers(1,&a->laughBuffer);
        alBufferData(a->laughBuffer,channels==1?AL_FORMAT_MONO16:AL_FORMAT_STEREO16,
                     samples,frames*channels*(int)sizeof(short),rate);
    }
    if(samples)free(samples);
    int error=0;
    a->decoder=stb_vorbis_open_filename(
        "ux0:data/deltarune/deltarunevita/music/man.ogg",&error,NULL);
    if(a->decoder){
        stb_vorbis_info info=stb_vorbis_get_info(a->decoder);
        a->channels=info.channels;a->rate=info.sample_rate;
        if(a->channels==1||a->channels==2){
            alGenBuffers(3,a->bgBuffers);
            for(int i=0;i<3;i++){
                if(egg_music_fill(a,a->bgBuffers[i]))
                    alSourceQueueBuffers(a->bgSource,1,&a->bgBuffers[i]);
            }
            alSourcePlay(a->bgSource);
            log_line("NATIVE_GXM=egg_ch1 music=man.ogg original_ch1 streaming=1");
        }
    }else log_line("NATIVE_GXM=egg_ch1 music_man_missing from_user_data");
}
static void egg_music_update(EggMusic *a){
    if(!a->active||!a->decoder||!a->bgSource)return;
    ALint processed=0;
    alGetSourcei(a->bgSource,AL_BUFFERS_PROCESSED,&processed);
    while(processed-->0){
        ALuint buffer=0;
        alSourceUnqueueBuffers(a->bgSource,1,&buffer);
        if(buffer && egg_music_fill(a,buffer))
            alSourceQueueBuffers(a->bgSource,1,&buffer);
    }
    ALint state=0,queued=0;
    alGetSourcei(a->bgSource,AL_SOURCE_STATE,&state);
    alGetSourcei(a->bgSource,AL_BUFFERS_QUEUED,&queued);
    if(queued>0&&state!=AL_PLAYING)alSourcePlay(a->bgSource);
}
static void egg_music_fx(EggMusic *a,int index){
    if(!a->active||index<0||index>=6||!a->fxBuffers[index])return;
    alSourceStop(a->fxSource);
    alSourcei(a->fxSource,AL_BUFFER,(ALint)a->fxBuffers[index]);
    alSourcePlay(a->fxSource);
}
static void egg_music_laugh(EggMusic *a){
    if(!a->active||!a->laughBuffer)return;
    alSourceStop(a->laughSource);
    alSourcei(a->laughSource,AL_BUFFER,(ALint)a->laughBuffer);
    alSourcePlay(a->laughSource);
    log_line("NATIVE_GXM=egg_intro audio=spamton_laugh_noise.ogg");
}
static void egg_music_close(EggMusic *a){
    if(!a->device)return;
    if(a->active){
        alSourceStop(a->bgSource);alSourceStop(a->fxSource);
        alSourceStop(a->laughSource);
        alDeleteSources(1,&a->bgSource);alDeleteSources(1,&a->fxSource);
        alDeleteSources(1,&a->laughSource);
        alDeleteBuffers(3,a->bgBuffers);
        for(int i=0;i<6;i++)if(a->fxBuffers[i])alDeleteBuffers(1,&a->fxBuffers[i]);
        if(a->laughBuffer)alDeleteBuffers(1,&a->laughBuffer);
    }
    if(a->decoder)stb_vorbis_close(a->decoder);
    alcMakeContextCurrent(NULL);
    if(a->context)alcDestroyContext(a->context);
    alcCloseDevice(a->device);
}
/* Original Chapter 1 world is 640x480, centered in Vita's 960x544. */
static void egg_world_img(NVGcontext *vg,int id,float x,float y,
                          float w,float h,float alpha){
    egg_image(vg,id,EGG_WORLD_X+x,EGG_WORLD_Y+y,w,h,alpha);
}
static int egg_intro_run(NVGcontext *vg,NVGXMwindow *window,
                         VitaSettings *settings,int nativeTrophies){
    log_line("NATIVE_GXM=egg_intro stage=begin custom_chapter0=1");
    log_line("NATIVE_GXM=egg_ch1 room=room_man authentic_assets=1");
    int bg=egg_load_png(vg,EGG_CH1_ROOT "room_man_tiles.png");
    int branches[3]={0};
    for(int i=1;i<=3;i++){
        char path[164];
        snprintf(path,sizeof(path),EGG_CH1_ROOT "spr_blocktree_parts_%d.png",i);
        branches[i-1]=egg_load_png(vg,path);
    }
    /* User-provided Chapter 1 style dialogue frame and animated star. */
    int dialogBox=egg_load_png(vg,EGG_ASSET_ROOT "dialogbox.png");
    int starFrames[6]={0};
    for(int i=0;i<6;i++){
        char filename[170];
        snprintf(filename,sizeof(filename),EGG_ASSET_ROOT "star_frames/star_%d.png",i);
        starFrames[i]=egg_load_png(vg,filename);
    }
    int block=egg_load_png(vg,EGG_CH1_ROOT "spr_blocktree_block_0.png");
    const char *dirs[]={"d","u","l","r"};
    int kris[4][4]={0};
    for(int dir=0;dir<4;dir++)for(int frame=0;frame<4;frame++){
        char path[178];
        snprintf(path,sizeof(path),EGG_CH1_ROOT "spr_kris%s_dark_%d.png",dirs[dir],frame);
        kris[dir][frame]=egg_load_png(vg,path);
    }
    /* A new prologue always starts in English. Changes made here are
     * immediate and persist to the real original/modded chapter 0. */
    int lang=0,available[10]={0};
    int languageCount=egg_available_languages(settings,available);
    VitaSettings_selectIntroLanguage(settings,"English");
    char msgLang[160];
    snprintf(msgLang,sizeof(msgLang),
      "NATIVE_GXM=egg_intro available_languages=%d base=English,Japanese mods=external_datawin_only",
      languageCount);
    log_line(msgLang);
    int sheet=0,flagIcon=0,optionSheet=0,promptImage=0,languageHelp=0;
    if(!egg_load_language_graphics(vg,lang,&sheet,&flagIcon,&optionSheet,
                                   &promptImage,&languageHelp)||
       bg<=0||dialogBox<=0||!starFrames[0]||
       !branches[0]||!branches[1]||!branches[2]||!kris[0][0]){
        log_line("NATIVE_GXM=egg_intro asset_load_failed");
        if(bg>0)nvgDeleteImage(vg,bg);
        if(dialogBox>0)nvgDeleteImage(vg,dialogBox);
        for(int k=0;k<6;k++)if(starFrames[k]>0)nvgDeleteImage(vg,starFrames[k]);
        if(sheet>0)nvgDeleteImage(vg,sheet);
        if(flagIcon>0)nvgDeleteImage(vg,flagIcon);
        if(optionSheet>0)nvgDeleteImage(vg,optionSheet);
        if(promptImage>0)nvgDeleteImage(vg,promptImage);
        if(languageHelp>0)nvgDeleteImage(vg,languageHelp);
        for(int i=0;i<3;i++)if(branches[i]>0)nvgDeleteImage(vg,branches[i]);
        for(int d=0;d<4;d++)for(int k=0;k<4;k++)
            if(kris[d][k]>0)nvgDeleteImage(vg,kris[d][k]);
        if(block>0)nvgDeleteImage(vg,block);
        return -1;
    }
    log_line("NATIVE_GXM=egg_intro default_language=English");
    EggMusic music;
    egg_music_init(&music);
    float playerX=300.0f,playerY=360.0f;
    unsigned frame=0,lastButtons=0;
    int stage=0,selectedYes=1,fade=0,success=0,userExit=0;
    unsigned dialogFrame=0;
    int facing=1,moving=0; /* Kris starts facing UP, towards the tree. */
    while(1){
        SceCtrlData pad={0};
        sceCtrlPeekBufferPositive(0,&pad,1);
        unsigned pressed=pad.buttons&~lastButtons;
        lastButtons=pad.buttons;
        moving=0;
        if(stage==0){
            if(pad.buttons&SCE_CTRL_UP){playerY-=2.6f;facing=1;moving=1;}
            else if(pad.buttons&SCE_CTRL_DOWN){playerY+=2.6f;facing=0;moving=1;}
            if(pad.buttons&SCE_CTRL_LEFT){playerX-=2.6f;facing=2;moving=1;}
            else if(pad.buttons&SCE_CTRL_RIGHT){playerX+=2.6f;facing=3;moving=1;}
            if(playerY<230)playerY=230;
            if(playerY>365)playerY=365;
            if(playerX<278)playerX=278;
            if(playerX>342)playerX=342;
            if((pressed&SCE_CTRL_CROSS)&&playerY<=286){
                stage=1;dialogFrame=0;
                egg_music_fx(&music,0);
                log_line("NATIVE_GXM=egg_ch1 tree_interaction=1");
            }
        }else if(stage==10){
            unsigned count=egg_glyph_counts[lang][10];
            /* Keep the teasing farewell readable for several seconds. */
            if(dialogFrame>=count*EGG_TYPE_FRAMES_PER_GLYPH+240 ||
               ((pressed&SCE_CTRL_CROSS) &&
                dialogFrame>=count*EGG_TYPE_FRAMES_PER_GLYPH+150)){
                log_line("NATIVE_GXM=egg_intro choice=NO joke_completed exit");
                userExit=1;break;
            }
        }else if(stage>=1&&stage<=8){
            if(stage==1 && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))){
                egg_change_language(settings,vg,&lang,
                   (pressed&SCE_CTRL_RIGHT)?1:-1,&sheet,&flagIcon,&optionSheet,
                   &promptImage,&languageHelp,available,languageCount);
                dialogFrame=0;egg_music_fx(&music,2);
            }
            if(stage==7 && (pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))){
                selectedYes=!selectedYes;egg_music_fx(&music,2);
            }
            if(pressed&SCE_CTRL_CIRCLE && stage>1&&stage<=8){
                --stage;dialogFrame=0;egg_music_fx(&music,2);
            }
            if(pressed&SCE_CTRL_CROSS){
                int slot=egg_stage_slot(stage);
                int full=egg_glyph_counts[lang][slot];
                if(stage==1){
                    /* Confirm language immediately, before any thank-you text. */
                    if(!egg_language_installed(settings,lang)){
                        egg_music_fx(&music,2);
                        log_line("NATIVE_GXM=egg_intro translation_missing");
                    }else{
                        stage=2;dialogFrame=0;
                        egg_music_fx(&music,3);
                        log_line("NATIVE_GXM=egg_intro language_confirmed greeting_next");
                    }
                }else if(dialogFrame/EGG_TYPE_FRAMES_PER_GLYPH<(unsigned)full){
                    dialogFrame=(unsigned)full*EGG_TYPE_FRAMES_PER_GLYPH;
                    egg_music_fx(&music,3);
                }else if(stage==7&&!selectedYes){
                    stage=10;dialogFrame=0;
                    egg_music_laugh(&music);
                    log_line("NATIVE_GXM=egg_intro choice=NO joke_before_exit");
                }else if(stage==8){
                    stage=9;fade=0;dialogFrame=0;
                    egg_music_fx(&music,5); /* AUDIO_APPEARANCE.wav */
                    log_line("NATIVE_GXM=egg_intro choice=YES audio=AUDIO_APPEARANCE.wav");
                }else{
                    stage++;dialogFrame=0;
                    egg_music_fx(&music,3);
                }
            }
        }
        if((stage>=2&&stage<=8)||stage==10){
            int slot=egg_stage_slot(stage);
            int full=egg_glyph_counts[lang][slot];
            if(dialogFrame/EGG_TYPE_FRAMES_PER_GLYPH<(unsigned)full &&
               dialogFrame%EGG_TYPE_FRAMES_PER_GLYPH==0)
                egg_music_fx(&music,4); /* original snd_txttor per letter */
        }
        if(stage==9&&fade<165)fade++;
        egg_music_update(&music);
        gxmBeginFrame();
        gxmClearColor(0,0,0,1);gxmClear();
        nvgBeginFrame(vg,960,544,1);
        nvgSave(vg);
        nvgScissor(vg,EGG_WORLD_X,EGG_WORLD_Y,EGG_WORLD_W,EGG_WORLD_H);
        egg_world_img(vg,bg,0,0,640,480,1);
        float siner=(float)frame+235.0f;
        egg_world_img(vg,branches[0],240,80,210,182,1);
        egg_world_img(vg,branches[1],240+sinf(siner/12)*2,
                      80+cosf(siner/20)*2,210,182,1);
        egg_world_img(vg,branches[2],240+sinf(siner/14),
                      80+cosf(siner/24),210,182,1);
        /* Chapter 1 uses spr_blocktree_block periodically around the canopy. */
        if(block>0 && stage<=8){
            for(int i=0;i<3;i++){
                unsigned life=(frame+i*17)%53;
                if(life<35){
                    float x=245+(float)((i*67+frame/15)%180);
                    float y=92+(float)(i*27)+(float)life*.72f;
                    egg_world_img(vg,block,x,y,32,20,
                                 life<9?(float)life/9:
                                 life>29?(float)(35-life)/6:0.85f);
                }
            }
        }
        int index=moving?(int)((frame/8)%4):0;
        int sprite=kris[facing][index]>0?kris[facing][index]:kris[0][0];
        egg_world_img(vg,sprite,playerX,playerY,38,76,1);
        nvgRestore(vg);
        if(stage==0){
            if(playerY>286){
                /* One transparent centered prompt with authentic CROSS
                 * sprite in place of the ASCII X, no border or backdrop. */
                /* Start the centered hint after 2 seconds, fade over 0.65s. */
                float promptAlpha=(float)((int)frame-120)/40.0f;
                if(promptAlpha>1.0f)promptAlpha=1.0f;
                if(promptAlpha>0.0f)
                    egg_image(vg,promptImage,25,448,910,64,promptAlpha);
            }else{
                /* Animated star from data/addons/ui/estrela.gif, small and
                 * centered where the old white 7px square was drawn. */
                int fi=(frame/10)%6;
                egg_image(vg,starFrames[fi],450,285,25,25,0.92f);
            }
        }else{
            /* Deltarune-style black dialogue box: uses the full 960-pixel
             * width. Deliberately no top border, as requested. */
            /* Use the *actual* dialogbox.png, not an approximated
             * rectangle. Its white-corner Deltarune border is centered. */
            egg_image(vg,dialogBox,26,351,908,178,1);
            int slot=egg_stage_slot(stage);
            if(stage==1&&!egg_language_installed(settings,lang))slot=9;
            int seen=stage==9?-1:(int)(dialogFrame/EGG_TYPE_FRAMES_PER_GLYPH);
            if(stage==1)
                /* Reuse the original JVE dialogue sheet. The old
                 * helper PNG was rasterized with Consolas after the source
                 * 8bitoperator font went missing, causing font switching. */
                egg_draw_text(vg,sheet,lang,slot,52,364,frame,-1);
            else
                egg_draw_text(vg,sheet,lang,slot,52,364,frame,seen);
            if(stage==1){
                /* Language is always the first dialog: a compact, separate
                 * choice card ABOVE the tree canopy, with left/right hints.
                 * The flag stays small, shaded and the label remains below. */
                /* Rounded, feathered charcoal shadow behind the language
                 * flag. The old hard purple 2px top rule is removed. */
                NVGpaint flagShadow=nvgBoxGradient(vg,282,38,396,140,8,20,
                       nvgRGBA(0,0,0,116),nvgRGBA(0,0,0,0));
                nvgBeginPath(vg);
                nvgRect(vg,261,17,438,182);
                nvgFillPaint(vg,flagShadow);
                nvgFill(vg);
                egg_image(vg,flagIcon,285,39,390,142,1);
                /* Pixel-font '<' and '>' glyphs (5 rows, 3 columns);
                 * yellow and gently floating horizontally. */
                const unsigned char left[5]={4,2,1,2,4};
                const unsigned char right[5]={1,2,4,2,1};
                float sway=sinf((float)frame*0.052f)*3.0f;
                for(int row=0;row<5;row++)
                    for(int col=0;col<3;col++){
                        if(left[row]&(1u<<col))
                            egg_panel(vg,273+sway+col*4,100+row*4,4,4,255,225,63,245);
                        if(right[row]&(1u<<col))
                            egg_panel(vg,680-sway+col*4,100+row*4,4,4,255,225,63,245);
                    }
            }
            if(stage==4){
                egg_panel(vg,909,493,12,12,
                          nativeTrophies?34:165,nativeTrophies?200:161,70,255);
            }
            if(stage==7){
                /* Sprite-based yellow choice with an intentional tiny shake.
                 * The old underline/selection bar is gone. */
                egg_draw_option(vg,optionSheet,0,selectedYes,
                                258,463,frame);
                egg_draw_option(vg,optionSheet,1,!selectedYes,
                                588,463,frame);
            }
            if(stage>=1&&stage<=8 && frame%28<13 &&
               dialogFrame/EGG_TYPE_FRAMES_PER_GLYPH >=
                  egg_glyph_counts[lang][slot] && stage!=7)
                egg_panel(vg,914,500,7,7,255,255,255,240);
        }
        if(stage==9){
            float alpha=(float)fade/65.0f;
            if(alpha>1.0f)alpha=1.0f;
            egg_panel(vg,0,0,960,544,0,0,0,(unsigned char)(alpha*255));
        }
        /* 48-frame fade-in from complete black on first launch. */
        if(frame<48){
            float alpha=(float)(48-frame)/48.0f;
            egg_panel(vg,0,0,960,544,0,0,0,(unsigned char)(alpha*255));
        }
        nvgEndFrame(vg);
        gxmEndFrame();
        gxmSwapBuffer();
        nvgxmCollectGarbage(vg);
        frame++;dialogFrame++;
        if(stage==9&&fade>=165){success=1;break;}
        sceKernelDelayThread(16000);
    }
    egg_music_close(&music);
    sceGxmFinish(window->context);
    if(bg>0)nvgDeleteImage(vg,bg);
    if(dialogBox>0)nvgDeleteImage(vg,dialogBox);
    for(int k=0;k<6;k++)if(starFrames[k]>0)nvgDeleteImage(vg,starFrames[k]);
    if(sheet>0)nvgDeleteImage(vg,sheet);
    if(flagIcon>0)nvgDeleteImage(vg,flagIcon);
    if(optionSheet>0)nvgDeleteImage(vg,optionSheet);
        if(promptImage>0)nvgDeleteImage(vg,promptImage);
        if(languageHelp>0)nvgDeleteImage(vg,languageHelp);
    if(block>0)nvgDeleteImage(vg,block);
    for(int i=0;i<3;i++)if(branches[i]>0)nvgDeleteImage(vg,branches[i]);
    for(int dir=0;dir<4;dir++)for(int k=0;k<4;k++)
        if(kris[dir][k]>0)nvgDeleteImage(vg,kris[dir][k]);
    if(success){
        /* Only YES marks completion. NO exits leaving prologue='s'.
         * Persist before relaunch (sceAppMgrLoadExec does not return on
         * success); all subsequent chapter0 boots skip the egg tree. */
        VitaSettings_markPrologueComplete(settings);
        log_line("NATIVE_GXM=prologue_config committed=n before_relaunch=1");
        log_line("NATIVE_GXM=egg_intro transition=original_chapter0 selected_language_saved");
        int rc=restart_into_chapter(0,window);
        if(rc<0)log_line("NATIVE_GXM=egg_intro chapter0_relaunch_failed");
        return rc;
    }
    return userExit?0:-1;
}
