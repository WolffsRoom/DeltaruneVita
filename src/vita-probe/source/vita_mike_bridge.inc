/* VitaGL parity with the Native GXM Mike controller.
 * No PCM is persisted: VitaMic uses only a worker-thread RMS level.
 * A 9-frame touch hold is a click when mic OFF; shorter taps are noise.
 * Only active for Chapter 4 Mike rooms, never normal UI/world navigation. */
typedef struct VitaGlMikeMouse {
    float x,y;
    bool initialized,down,touching;
    unsigned touchFrames;
} VitaGlMikeMouse;
static VitaGlMikeMouse vitaMikeMouse={0};
static bool vita_mike_has_scene(Runner* runner){
    if(!runner||!runner->currentRoom||!runner->dataWin)return false;
    static Runner*cachedRunner=NULL;
    static int cachedRoom=-99999;
    static bool cachedResult=false;
    static uint64_t cachedUs=0;
    uint64_t now=sceKernelGetProcessTimeWide();
    if(cachedRunner==runner&&cachedRoom==runner->currentRoomIndex &&
       now-cachedUs<100000ull)return cachedResult;
    cachedRunner=runner;cachedRoom=runner->currentRoomIndex;
    cachedUs=now;cachedResult=false;
    for(ptrdiff_t k=0;k<arrlen(runner->instances);k++){
        Instance*i=runner->instances[k];
        if(!i||!i->active||i->destroyed||i->objectIndex<0||
           (uint32_t)i->objectIndex>=runner->dataWin->objt.count)continue;
        const char*nm=runner->dataWin->objt.objects[i->objectIndex].name;
        if(nm&&(!strcmp(nm,"obj_mike_controller")||
                !strcmp(nm,"obj_mike_battle_test"))){
            cachedResult=true;break;
        }
    }
    return cachedResult;
}
static bool vita_mike_mouse_update(Runner*runner,const SceCtrlData*pad,
                                   bool enabled,bool micOn,int sensitivity){
    if(!runner||!runner->mouse||!pad)return false;
    if(!enabled){
        if(vitaMikeMouse.down)RunnerMouse_onButtonUp(runner->mouse,1);
        vitaMikeMouse.down=false;vitaMikeMouse.initialized=false;
        vitaMikeMouse.touching=false;vitaMikeMouse.touchFrames=0;
        return false;
    }
    if(!vitaMikeMouse.initialized){
        vitaMikeMouse.x=480;vitaMikeMouse.y=272;
        vitaMikeMouse.initialized=true;
        log_line("VITAGL_MIKE=mouse_bridge enabled=1 touch=cursor right_stick=cursor left_stick_dpad=player");
    }
    SceTouchData touch={0};
    bool down=sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1)>0 &&
              touch.reportNum>0;
    if(down){
        vitaMikeMouse.x=(float)touch.report[0].x*(960.0f/1919.0f);
        vitaMikeMouse.y=(float)touch.report[0].y*(544.0f/1087.0f);
    }else{
        /* Right stick steers the cursor; left stick and D-pad move Kris. */
        int rx=(int)pad->rx-128,ry=(int)pad->ry-128;
        if(sensitivity<1)sensitivity=1;
        if(sensitivity>10)sensitivity=10;
        const float speed=7.0f*((float)sensitivity/5.0f)/128.0f;
        vitaMikeMouse.x+=(abs(rx)>19?rx:0)*speed;
        vitaMikeMouse.y+=(abs(ry)>19?ry:0)*speed;
    }
    /* The game's own spr_mike_hand is drawn at the cursor coordinate.
     * Clamp the complete hand inside the active FBO viewport; never
     * let touch/held-click position place half of it outside the room. */
    float left=fmaxf(0.0f,(float)runner->viewportX);
    float top=fmaxf(0.0f,(float)runner->viewportY);
    float right=fminf(960.0f,left+(float)runner->viewportW);
    float bottom=fminf(544.0f,top+(float)runner->viewportH);
    if(right-left<32.0f){left=0;right=960;}
    if(bottom-top<32.0f){top=0;bottom=544;}
    const float inset=14.0f;
    vitaMikeMouse.x=fmaxf(left+inset,fminf(right-inset,vitaMikeMouse.x));
    vitaMikeMouse.y=fmaxf(top+inset,fminf(bottom-inset,vitaMikeMouse.y));
    Runner_updateMousePosition(runner,960,544,vitaMikeMouse.x,vitaMikeMouse.y);
    vitaMikeMouse.touching=down;
    if(down){if(vitaMikeMouse.touchFrames<120)vitaMikeMouse.touchFrames++;}
    else vitaMikeMouse.touchFrames=0;
    bool click=(pad->buttons&(SCE_CTRL_CROSS|SCE_CTRL_LTRIGGER|
                              SCE_CTRL_RTRIGGER))!=0 ||
                (down&&(micOn||vitaMikeMouse.touchFrames>=9));
    if(click&&!vitaMikeMouse.down)RunnerMouse_onButtonDown(runner->mouse,1);
    if(!click&&vitaMikeMouse.down)RunnerMouse_onButtonUp(runner->mouse,1);
    vitaMikeMouse.down=click;
    return true;
}
typedef struct VitaGlMikeMic {
    int stage;
    bool wasDown;
    uint64_t lastTap;
    VMContext* vm;
    int volumeVar,useMicVar,rightClickVar;
    int objIndex;
    const DataWin* data;
} VitaGlMikeMic;
static VitaGlMikeMic vitaMikeMic={0};
static void vita_mike_mic_update(Runner*runner,const VitaSettings*settings,
                                 bool active){
    if(!runner||!settings||!runner->vmContext)return;
    VitaGlMikeMic*mic=&vitaMikeMic;
    if(mic->vm!=runner->vmContext){
        mic->vm=runner->vmContext;
        mic->volumeVar=VM_getOrAllocateVarID(mic->vm,"microphone_volume");
        mic->useMicVar=VM_getOrAllocateVarID(mic->vm,"use_mic");
        mic->rightClickVar=VM_getOrAllocateVarID(mic->vm,"right_click_mic");
        mic->data=NULL;mic->wasDown=false;mic->lastTap=0;mic->stage=0;
    }
    if(!active){
        mic->stage=0;mic->wasDown=false;mic->lastTap=0;
        return;
    }
    if(runner->vmContext->globalScopeInstance){
        Instance*global=runner->vmContext->globalScopeInstance;
        if(mic->useMicVar>=0 &&
           !RValue_toBool(Instance_getSelfVar(global,mic->useMicVar)))
            Instance_setSelfVar(global,mic->useMicVar,RValue_makeBool(true));
        if(mic->rightClickVar>=0 &&
           RValue_toReal(Instance_getSelfVar(global,mic->rightClickVar))!=0)
            Instance_setSelfVar(global,mic->rightClickVar,RValue_makeReal(0));
    }
    int level=0;
    if(settings->microphoneEnabled){
        mic->stage=0;mic->wasDown=false;
        if(VitaMic_isAvailable()){
            level=VitaMic_getLevel()*settings->microphoneSensitivity/5;
            if(level>100)level=100;
        }
    }else{
        SceTouchData touch={0};
        bool down=sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1)>0&&touch.reportNum>0;
        uint64_t now=sceKernelGetProcessTimeWide();
        if(down&&!mic->wasDown){
            int side=touch.report[0].x>=960?1:-1;
            if(side>0&&(!mic->lastTap||now-mic->lastTap>450000))
                mic->stage=0;
            mic->stage+=side;
            if(mic->stage<0)mic->stage=0;
            if(mic->stage>4)mic->stage=4;
            mic->lastTap=now;
            static unsigned counter=0;
            if(counter++<25){
                char msg[120];
                snprintf(msg,sizeof(msg),
                   "VITAGL_MIKE=touch_noise side=%s stage=%d level=%d",
                    side>0?"right":"left",mic->stage,mic->stage*25);
                log_line(msg);
            }
        }
        mic->wasDown=down;
        level=down?mic->stage*25:0;
    }
    if(!runner->dataWin||mic->volumeVar<0)return;
    if(mic->data!=runner->dataWin){
        mic->data=runner->dataWin;mic->objIndex=-1;
        for(uint32_t i=0;i<runner->dataWin->objt.count;i++){
            const char*nm=runner->dataWin->objt.objects[i].name;
            if(nm&&strcmp(nm,"obj_mike_controller")==0){
                mic->objIndex=(int)i;break;
            }
        }
    }
    if(mic->objIndex<0)return;
    for(ptrdiff_t i=0;i<arrlen(runner->instances);i++){
        Instance*inst=runner->instances[i];
        if(!inst||inst->destroyed||!inst->active||
           inst->objectIndex!=mic->objIndex)continue;
        Instance_setSelfVar(inst,mic->volumeVar,RValue_makeReal(level));
        break;
    }
}
