/* Native version of the playable Vita border rule path.
 * Border assets live with the user's legitimate game data, not in the VPK. */
extern int g_nativeBorderMode;
extern int g_nativeBetterBorders;
extern int g_vitaActiveChapter;
static int bs_border_global(Runner* runner,const char* name,int def) {
 if(!runner||!runner->vmContext||!runner->vmContext->globalScopeInstance)return def;
 int32_t id=shget(runner->vmContext->varNameMap,name);
 if(id<0)return def;
 RValue v=Instance_getSelfVar(runner->vmContext->globalScopeInstance,id);
 return v.type==RVALUE_UNDEFINED?def:RValue_toInt32(v);
}
static Instance* bs_border_first_object(Runner* runner,const char* name) {
 if(!runner||!runner->instancesByObject||!runner->dataWin||
    !runner->vmContext||!name)return NULL;
 int obj=shget(runner->assetsByName,name);
 if(obj<0||(uint32_t)obj>=runner->dataWin->objt.count)return NULL;
 Instance** found=runner->instancesByObject[obj];
 for(ptrdiff_t i=0;i<arrlen(found);i++){
     Instance* inst=found[i];
     if(inst&&inst->active&&!inst->destroyed)return inst;
 }
 return NULL;
}
static int bs_border_inst_int(Runner* r,Instance*i,const char* name,int def){
 if(!i||!r||!r->vmContext)return def;
 int id=shget(r->vmContext->varNameMap,name);
 if(id<0)return def;
 RValue v=Instance_getSelfVar(i,id);
 return v.type==RVALUE_UNDEFINED?def:RValue_toInt32(v);
}
static float bs_border_inst_real(Runner*r,Instance*i,const char*name,float def){
 if(!i||!r||!r->vmContext)return def;
 int id=shget(r->vmContext->varNameMap,name);
 if(id<0)return def;
 RValue v=Instance_getSelfVar(i,id);
 return v.type==RVALUE_UNDEFINED?def:(float)RValue_toReal(v);
}
static const char* bs_border_sprite_name(Runner*r,int id){
 if(!r||!r->dataWin||id<0||(uint32_t)id>=r->dataWin->sprt.count)return NULL;
 const char* name=r->dataWin->sprt.sprites[id].name;
 return name&&strncmp(name,"border_",7)==0?name:NULL;
}
static void bs_border_sprite_path(const char*name,char*out,unsigned cap){
 if(!name||!out||!cap){if(out&&cap)out[0]=0;return;}
 size_t n=strlen(name);
 snprintf(out,cap,n>2&&!strcmp(name+n-2,"_0")?
  "ux0:data/deltarune/deltarunevita/borders/%s.png":
  "ux0:data/deltarune/deltarunevita/borders/%s_0.png",name);
}
static float bs_border_clamp(float v) {
 return v<0?0:v>1?1:v;
}
/* Chapter 1 VitaGL precedence: exact config / chapter_1, Better
 * Borders region override, then original Chapter 1 fallback. */
static int bs_ch1_custom_border(const char*room,char*out,size_t cap){
    static int loaded=0;
    static char config[4096];
    if(!loaded){
        loaded=1;
        FILE*f=fopen("ux0:data/deltarune/deltarunevita/borders/borders_config.txt","rb");
        if(f){
            size_t n=fread(config,1,sizeof(config)-1,f);
            config[n]=0;fclose(f);
        }
    }
    if(!room||!out||cap<2)return 0;
    const char*keys[2]={room,"chapter_1"};
    for(unsigned pass=0;pass<2;pass++){
        char pattern[148];
        snprintf(pattern,sizeof(pattern),"%.128s=",keys[pass]);
        size_t len=strlen(pattern);
        const char*line=config;
        while(*line){
            const char*end=strpbrk(line,"\r\n");
            if(!end)end=line+strlen(line);
            if((size_t)(end-line)>=len && strncmp(line,pattern,len)==0){
                const char*val=line+len;
                size_t n=(size_t)(end-val);
                while(n&&(val[n-1]==' '||val[n-1]=='\t'))--n;
                if(n==4&&(!strncmp(val,"NONE",4)||!strncmp(val,"none",4))){
                    out[0]=0;return 2;
                }
                if(n>0&&n<95){
                    snprintf(out,cap,
                        "ux0:data/deltarune/deltarunevita/borders/%.*s",(int)n,val);
                    return 1;
                }
            }
            line=end;
            while(*line=='\r'||*line=='\n')++line;
        }
    }
    return 0;
}
static int bs_ch1_is_lowercase_room(const char*room){
    if(!room)return 0;
    for(const char*p=room;*p;p++)
        if(*p>='A'&&*p<='Z')return 0;
    return 1;
}
static const char*bs_ch1_default_border(const char*room){
    if(bs_ch1_is_lowercase_room(room)){
        if(!strncmp(room,"room_town",9)||!strncmp(room,"room_beach",10))
            return "border_lw_town_0.png";
        if(!strncmp(room,"room_cc",7)||!strncmp(room,"room_forest",11)||
           !strncmp(room,"room_field",10)||!strncmp(room,"room_castle",11)||
           !strncmp(room,"room_shop",9)||!strncmp(room,"room_legend",11))
            return "border_dw_castletown_0.png";
    }
    return "border_line_1080_0.png";
}
static void bs_native_border_draw(BSGxmRenderer* b){
 if(!b||!b->vg||!b->base.runner||g_nativeBorderMode==0)return;
 Runner* runner=b->base.runner;
 const char* room=runner->currentRoom?runner->currentRoom->name:NULL;
 /* Same gating as the VitaGL border path (vita_borders.c roomBorder):
  * nothing before the chapter menu has been reached (startup, failsafe,
  * logo, legend, title), nothing on menus, and no art on Game Over /
  * failure / black-fade rooms, whose surround must stay black. */
 static int chapterMenuSeen=0,gameplayBorders=0;
 /* Detect first frame of door opening, before GameMaker changes rooms. */
 static int schoolDoorBlack=0,fromBlack=0,previousRoom=-999;
 if(!room||g_vitaActiveChapter<=0)return;
 if(previousRoom!=runner->currentRoomIndex){
   if(strcmp(room,"room_schooldoor")==0)schoolDoorBlack=0;
   previousRoom=runner->currentRoomIndex;
 }
 if(strcmp(room,"room_schooldoor")==0){
   Instance* door=bs_border_first_object(runner,"obj_darkdoor");
   if(door && door->active && door->visible){
     const char* spriteName=door->spriteIndex>=0 &&
       (uint32_t)door->spriteIndex<runner->dataWin->sprt.count?
       runner->dataWin->sprt.sprites[door->spriteIndex].name:NULL;
     if((spriteName && (!strcmp(spriteName,"spr_darkdoor_open") ||
                        !strcmp(spriteName,"spr_giantdarkdoor_white"))) ||
        (door->imageSpeed>0.0f && door->imageIndex>=1.0f)){
       if(!schoolDoorBlack)bs_trace("NATIVE_GXM=border_schooldoor animation_started black=1");
       schoolDoorBlack=1;
     }
   }
 }
 if((schoolDoorBlack && strcmp(room,"room_schooldoor")==0) ||
    strcmp(room,"room_dw_castle_area_1")==0){
   if(b->borderImage>0){nvgDeleteImage(b->vg,b->borderImage);b->borderImage=0;}
   if(b->borderOldImage>0){nvgDeleteImage(b->vg,b->borderOldImage);b->borderOldImage=0;}
   b->borderW=b->borderH=b->borderOldW=b->borderOldH=0;
   b->borderPath[0]=0;
   b->borderTransition=0.0f;
   fromBlack=1;
   return;
 }

 if(VitaBorderRules_isMenuRoom(room)){
  if(strstr(room,"PLACE_MENU")!=NULL){chapterMenuSeen=1;gameplayBorders=0;}
  return;
 }
 if(strncmp(room,"room_gameover",13)==0||strcmp(room,"PLACE_FAILURE")==0||
    strcmp(room,"room_dw_castle_area_1")==0||
    (g_vitaActiveChapter==3&&strcmp(room,"room_ed")==0)||
    (g_vitaActiveChapter==4&&strcmp(room,"room_cc_fountain")==0))return;
 if(!gameplayBorders){
  if(!chapterMenuSeen)return;
  gameplayBorders=1;
 }
 if(g_nativeBorderMode==2){
  nvgBeginPath(b->vg);
  nvgRect(b->vg,113,0,4,DISPLAY_HEIGHT);
  nvgRect(b->vg,843,0,4,DISPLAY_HEIGHT);
  nvgFillColor(b->vg,nvgRGBA(255,255,255,255));nvgFill(b->vg);
  return;
 }
 VitaBorderRuleState state={0};
 state.chapter=g_vitaActiveChapter;state.roomIndex=runner->currentRoomIndex;
 state.plot=bs_border_global(runner,"plot",0);
 state.darkzone=bs_border_global(runner,"darkzone",0);
 state.gameWon=bs_border_global(runner,"gamewon",0);
 state.flag1324=-1;
 if(g_vitaActiveChapter==5){
  Instance* petals=bs_border_first_object(runner,"obj_sakurafubuki_new");
  if(petals&&bs_border_inst_int(runner,petals,"transition_stage",0)>=1)
   state.sceneFlags|=VITA_BORDER_SCENE_CH5_GARDEN_REVEAL;
  Instance* date=bs_border_first_object(runner,"obj_date_controller");
  if(date&&bs_border_inst_int(runner,date,"draw_box_timer",0)>=500)
   state.sceneFlags|=VITA_BORDER_SCENE_CH5_PINK_ALT;
  Instance* pillars=bs_border_first_object(runner,"obj_flowery_towery_pillars");
  if(pillars){
   int bg=bs_border_inst_int(runner,pillars,"bg_change",-1);
   if(bg>=30&&bg<=120)state.sceneFlags|=VITA_BORDER_SCENE_CH5_CASTLE_TOP;
   if(bg>90&&bg<=120)state.sceneFlags|=VITA_BORDER_SCENE_HIDE_WHITE;
   if(bg>120)state.sceneFlags|=VITA_BORDER_SCENE_CH5_CASTLE_GOLD;
  }
 }
 if(VitaBorderRules_shouldBlankSurround(state.chapter,room,state.sceneFlags))return;
 /* Tenna snap is signaled by the original GML SFX, not by the new room.
  * Keep the TV border before snap and blue stars from the snap onward. */
 if(g_vitaActiveChapter==3 && room &&
    strcmp(room,"room_dw_snow_zone")==0){
   extern int g_nativeCh3TennaSnap;
   if(g_nativeCh3TennaSnap)
      state.sceneFlags|=VITA_BORDER_SCENE_CH3_BLUE_STARS;
   else state.sceneFlags|=VITA_BORDER_SCENE_CH3_TV_BLACK;
 }
 VitaBorderDecision choice=VitaBorderRules_resolve(room,&state);
 const char* file=VitaBorderRules_filename(choice.asset);
 char desired[192]={0};
 Instance* controller=bs_border_first_object(runner,"obj_border_controller");
 int currentId=bs_border_inst_int(runner,controller,"_border_image",-1);
 int pendingId=bs_border_inst_int(runner,controller,"_border_image_temp",currentId);
 const char* current=bs_border_sprite_name(runner,currentId);
 const char* pending=bs_border_sprite_name(runner,pendingId);
 int explicitXfade=bs_border_inst_int(runner,controller,"_border_crossfade",0);
 int isCrossfade=controller && current && pending &&
   VitaBorderRules_controllerCrossfade(g_vitaActiveChapter,currentId,pendingId,
                                     explicitXfade);
 b->borderControllerCrossfade=isCrossfade;
 b->borderControllerAlpha=isCrossfade?bs_border_clamp(
    bs_border_inst_real(runner,controller,"_border_image_temp_alpha",0)):1.0f;
 const char* controllerName=isCrossfade?pending:current;
 /* Native obj_border_controller knows live show_border/set_border changes.
  * It has priority over the static room name map whenever Better Borders is
  * disabled; including chapter 1 after the transition rooms. */
 if(g_vitaActiveChapter>=2 && controllerName&&strlen(controllerName)<95 &&
    !(g_vitaActiveChapter==3 && room &&
      strcmp(room,"room_dw_snow_zone")==0)) {
  bs_border_sprite_path(controllerName,desired,sizeof(desired));
  SceIoStat st;
  if(sceIoGetstat(desired,&st)<0)desired[0]=0;
 }
 /* The original Tenna snap script switches to border_dw_blue_stars_0.
  * Its live controller image must override the generic border3 mod,
  * but only in the intended gameshow cutscene. */
 extern int g_nativeCh3TennaSnap;
 if(g_vitaActiveChapter==3 && room &&
    strcmp(room,"room_dw_tv_cutscene1g")==0 &&
    ((controllerName && strcmp(controllerName,"border_dw_blue_stars")==0) ||
     g_nativeCh3TennaSnap)){
    char blue[192];
    bs_border_sprite_path("border_dw_blue_stars",blue,sizeof(blue));
    SceIoStat st;
    if(sceIoGetstat(blue,&st)>=0){
        snprintf(desired,sizeof(desired),"%s",blue);
        static unsigned report=0;
        if(report++<3)bs_trace("NATIVE_GXM=ch3_tenna_blue_border source=scene_control");
    }
 }
 /* Chapter 1 Better Borders is region-specific, never globally border1.
  * Ch2–5 use their own native GameMaker controller and resolver. */
 int explicitNone=0;
 if(g_vitaActiveChapter==1 && g_nativeBorderMode==1){
    int custom=bs_ch1_custom_border(room,desired,sizeof(desired));
    explicitNone=(custom==2);
    if(custom==0 && g_nativeBetterBorders &&
       bs_ch1_is_lowercase_room(room)){
        const char*alt=NULL;
        if(!strncmp(room,"room_cc",7))alt="border1.png";
        else if(!strncmp(room,"room_field",10))alt="border2.png";
        else if(!strncmp(room,"room_forest",11))alt="border3.png";
        if(alt){
            snprintf(desired,sizeof(desired),
              "ux0:data/deltarune/deltarunevita/mods/Borders/%s",alt);
            SceIoStat st;
            if(sceIoGetstat(desired,&st)<0)desired[0]=0;
        }
    }
    if(custom==0&&!desired[0])
        snprintf(desired,sizeof(desired),
          "ux0:data/deltarune/deltarunevita/borders/%s",
          bs_ch1_default_border(room));
 } else if(!desired[0]&&file){
    snprintf(desired,sizeof(desired),
      "ux0:data/deltarune/deltarunevita/borders/%s",file);
 }
 if(explicitNone)desired[0]=0;
 if(strcmp(desired,b->borderPath)!=0){
  /* Crossfade live border changes. Old texture is retained for the fade,
   * then freed; this also handles room transitions back to fallback art. */
  if(b->borderOldImage>0) {
      nvgDeleteImage(b->vg,b->borderOldImage);
      b->borderOldImage=0;
  }
  b->borderOldImage=b->borderImage;
  b->borderOldW=b->borderW;b->borderOldH=b->borderH;
  b->borderImage=0;
  b->borderTransition=fromBlack?0.0f:(b->borderOldImage>0?
     (b->borderControllerCrossfade?b->borderControllerAlpha:0.0f):1.0f);
  if(fromBlack)bs_trace("NATIVE_GXM=border_castle_reveal fade_from_black=1");
  fromBlack=0;
  b->borderW=b->borderH=0;
  snprintf(b->borderPath,sizeof(b->borderPath),"%s",desired);
  b->borderChanges++;
  if(desired[0]) {
   FILE* f=fopen(desired,"rb");
   if(f) {
    if(fseek(f,0,SEEK_END)==0){
     long size=ftell(f);
     if(size>0 && size<12582912 && fseek(f,0,SEEK_SET)==0){
      uint8_t* bytes=malloc((size_t)size);
      if(bytes && fread(bytes,1,(size_t)size,f)==(size_t)size) {
       int w=0,h=0;
       uint8_t* rgba=ImageDecoder_decodeToRgba(bytes,(size_t)size,0,&w,&h);
       if(rgba&&w>0&&h>0&&w<=2048&&h<=2048){
        b->borderImage=gxmRGBA4444Upload(b->vg,w,h,NVG_IMAGE_NEAREST,rgba);
        b->borderW=w;b->borderH=h;
       }
       free(rgba);
      }
      free(bytes);
     }
    }
    fclose(f);
   }
   char line[280];
   snprintf(line,sizeof(line),"NATIVE_GXM=border room=%.70s image=%d path=%.150s",
         room,b->borderImage,desired);
   bs_trace(line);
   if(b->borderImage<=0)b->borderFailures++;
  }
 }
 if(b->borderControllerCrossfade && b->borderOldImage>0)
    b->borderTransition=b->borderControllerAlpha;
 float borderMix=bs_border_clamp(b->borderTransition);
 if(g_vitaActiveChapter==1 && !b->borderControllerCrossfade)
    borderMix=borderMix*borderMix*(3.0f-2.0f*borderMix);
 /* VitaGL compositing order: new border stays opaque BEHIND fading old.
  * Drawing both with reciprocal half opacity darkened Chapter 1 midfade.
  * Retain fade-in when entering from intentionally black surroundings. */
 if(b->borderImage>0)
    bs_image_rect(b,b->borderImage,b->borderW,b->borderH,
       0,0,(float)b->borderW,(float)b->borderH,
       0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,
       b->borderOldImage>0?1.0f:borderMix);
 if(b->borderOldImage>0 && borderMix<1.0f)
    bs_image_rect(b,b->borderOldImage,b->borderOldW,b->borderOldH,
       0,0,(float)b->borderOldW,(float)b->borderOldH,
       0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,1.0f-borderMix);
 if(controller && g_nativeBorderMode==1){
     float a=bs_border_clamp(bs_border_inst_real(runner,controller,
          g_vitaActiveChapter==2?"border_alpha":"overlay_alpha",
          g_vitaActiveChapter==2?1.0f:0.0f));
     if(g_vitaActiveChapter==2)a=1.0f-a;
     uint32_t col=(uint32_t)bs_border_inst_int(runner,controller,"overlay_color",0);
     if(a>.001f){
        nvgBeginPath(b->vg);
        nvgRect(b->vg,0,0,117,DISPLAY_HEIGHT);
        nvgRect(b->vg,843,0,DISPLAY_WIDTH-843,DISPLAY_HEIGHT);
        nvgFillColor(b->vg,bs_col(col,a));nvgFill(b->vg);
     }
     float red=bs_border_clamp(bs_border_inst_real(
          runner,controller,"_border_red_alpha",0));
     if(red>.001f){
        nvgBeginPath(b->vg);
        nvgRect(b->vg,0,0,117,DISPLAY_HEIGHT);
        nvgRect(b->vg,843,0,DISPLAY_WIDTH-843,DISPLAY_HEIGHT);
        nvgFillColor(b->vg,bs_col(0x0000ffu,red));nvgFill(b->vg);
     }
 }
 if(b->borderTransition<1.0f && !b->borderControllerCrossfade)
   b->borderTransition+=1.0f/24.0f;
 if(b->borderTransition>=1.0f){
   b->borderTransition=1.0f;
   if(b->borderOldImage>0){
     nvgDeleteImage(b->vg,b->borderOldImage);b->borderOldImage=0;
   }
 }
}
