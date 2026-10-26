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
static void bs_native_border_draw(BSGxmRenderer* b){
 if(!b||!b->vg||!b->base.runner||g_nativeBorderMode==0)return;
 Runner* runner=b->base.runner;
 const char* room=runner->currentRoom?runner->currentRoom->name:NULL;
 /* Same gating as the VitaGL border path (vita_borders.c roomBorder):
  * nothing before the chapter menu has been reached (startup, failsafe,
  * logo, legend, title), nothing on menus, and no art on Game Over /
  * failure / black-fade rooms, whose surround must stay black. */
 static int chapterMenuSeen=0,gameplayBorders=0;
 if(!room||g_vitaActiveChapter<=0)return;
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
 if(!g_nativeBetterBorders&&controllerName&&strlen(controllerName)<95) {
  bs_border_sprite_path(controllerName,desired,sizeof(desired));
  SceIoStat st;
  if(sceIoGetstat(desired,&st)<0)desired[0]=0;
 }
 if(!desired[0]&&file) {
  if(g_nativeBetterBorders) {
   const char* alt=g_vitaActiveChapter==1?"border1.png":
                   g_vitaActiveChapter==2?"border2.png":"border3.png";
   snprintf(desired,sizeof(desired),
     "ux0:data/deltarune/deltarunevita/mods/Borders/%s",alt);
   SceIoStat st;
   if(sceIoGetstat(desired,&st)<0)desired[0]=0;
  }
  if(!desired[0])
   snprintf(desired,sizeof(desired),
     "ux0:data/deltarune/deltarunevita/borders/%s",file);
 }
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
  b->borderTransition=b->borderOldImage>0?
     (b->borderControllerCrossfade?b->borderControllerAlpha:0.0f):1.0f;
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
 if(b->borderOldImage>0 && b->borderTransition<1.0f) {
    bs_image_rect(b,b->borderOldImage,b->borderOldW,b->borderOldH,
       0,0,(float)b->borderOldW,(float)b->borderOldH,
       0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,1.0f-b->borderTransition);
 }
 if(b->borderImage>0)
  bs_image_rect(b,b->borderImage,b->borderW,b->borderH,
     0,0,(float)b->borderW,(float)b->borderH,
     0,0,DISPLAY_WIDTH,DISPLAY_HEIGHT,b->borderTransition);
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
 if(b->borderOldImage>0){
   if(!b->borderControllerCrossfade)b->borderTransition+=1.0f/24.0f;
   if(b->borderTransition>=1.0f){
      nvgDeleteImage(b->vg,b->borderOldImage);
      b->borderOldImage=0;b->borderTransition=1.0f;
   }
 }
}
