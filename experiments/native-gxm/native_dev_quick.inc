/* Native GXM developer quick tests, resolved from THIS chapter's real room list.
 * Never assume IDs across mods, translations or patched data.win.
 * Rankings use room dimensions and available header/name evidence. They are
 * candidates, not claims to measure runtime shader count or guarantee story
 * variables. Actual jump uses the existing fenced DEV Room Navigator path. */
#ifndef DELTARUNE_GXM_DEV_QUICK_INC
#define DELTARUNE_GXM_DEV_QUICK_INC
#define GXM_DEV_QUICK_COUNT 5
static const char*const gxmDevQuickLabels[GXM_DEV_QUICK_COUNT]={
    "BOSS / FINAL","LARGEST ROOM","COMPLEX ROOM",
    "SHADERS / LIGHT","HEAVY EFFECTS"
};
static int gxmDevNameHas(const char*name,const char*key){
    if(!name||!key)return 0;
    return strstr(name,key)!=NULL;
}
static void gxmDevQuickBuild(const DataWin*dw,int chapter,int result[5]){
    for(int j=0;j<GXM_DEV_QUICK_COUNT;j++)result[j]=-1;
    if(!dw)return;
    double scores[5]={-1.0,-1.0,-1.0,-1.0,-1.0};
    for(uint32_t k=0;k<dw->room.count;k++){
        const Room*room=&dw->room.rooms[k];
        const char*name=room->name;
        if(!room->present||!name||room->width<32||room->height<32)continue;
        if(strncmp(name,"room_",5)!=0 && strncmp(name,"PLACE_",6)!=0)continue;
        double area=(double)room->width*room->height;
        double roomSize=(double)room->width+(double)room->height;
        int finalMatch=gxmDevNameHas(name,"final")||gxmDevNameHas(name,"boss")||
             gxmDevNameHas(name,"gigaqueen")||gxmDevNameHas(name,"queen_battle")||
             gxmDevNameHas(name,"king_battle")||gxmDevNameHas(name,"battle_king")||
             gxmDevNameHas(name,"tenna")||gxmDevNameHas(name,"titan")||
             gxmDevNameHas(name,"knight")||gxmDevNameHas(name,"flowey")||
             gxmDevNameHas(name,"spamton")||gxmDevNameHas(name,"jevil");
        /* Chapters with unnamed boss arena still expose their latest
         * real battle room instead of an empty quick-test destination. */
        double boss=finalMatch?1000000.0+(double)k:
            (gxmDevNameHas(name,"battle")?10000.0+(double)k:-1.0);
        if(chapter==2&&gxmDevNameHas(name,"gigaqueen"))boss+=2000000.0;
        if(chapter==3&&gxmDevNameHas(name,"tenna"))boss+=2000000.0;
        if(chapter==4&&gxmDevNameHas(name,"titan"))boss+=2000000.0;
        if(chapter==5&&(gxmDevNameHas(name,"knight")||gxmDevNameHas(name,"final")))boss+=2000000.0;
        double complexity=roomSize*0.3+
           (room->payloadLoaded?room->gameObjectCount*100.0+room->layerCount*500.0+room->tileCount:0.0);
        double light=(gxmDevNameHas(name,"town")||gxmDevNameHas(name,"shadow")||
            gxmDevNameHas(name,"light")||gxmDevNameHas(name,"castle")||
            gxmDevNameHas(name,"night")||gxmDevNameHas(name,"tenna")||
            gxmDevNameHas(name,"tv_zone")||gxmDevNameHas(name,"cyber_maze"))?
                100000.0+roomSize: -1.0;
        double effects=(gxmDevNameHas(name,"battle")||gxmDevNameHas(name,"bullet")||
            gxmDevNameHas(name,"music")||gxmDevNameHas(name,"viro")||
            gxmDevNameHas(name,"rain")||gxmDevNameHas(name,"laser")||
            gxmDevNameHas(name,"dark")||gxmDevNameHas(name,"fire")||
            gxmDevNameHas(name,"tv_zone")||gxmDevNameHas(name,"queen"))?
                100000.0+roomSize+(double)k*0.01: -1.0;
        double candidate[5]={boss,area,complexity,light,effects};
        for(int j=0;j<GXM_DEV_QUICK_COUNT;j++)
            if(candidate[j]>scores[j]){scores[j]=candidate[j];result[j]=(int)k;}
    }
}
#endif
