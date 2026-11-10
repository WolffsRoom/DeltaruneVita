/* Vita native translation of Chapter 2's obj_firework_explosion Step/Draw.
 * The bytecode event is skipped ONLY if every expected per-particle GML
 * array exists, fits the pixel count, and has one owner (no CoW aliasing).
 * The exact original code is retained in data.win for VM fallback.
 */
#ifdef PLATFORM_VITA
#include "gml_array.h"
#include <math.h>
#include <string.h>

enum {
 FW_PIXELS,FW_TIME,FW_X,FW_Y,FW_VSP,FW_MAINTAIN,FW_GRAV,FW_MAXVSP,
 FW_GRAVOFFSET,FW_MAXOFFSET,FW_PROG,FW_SPD,FW_H,FW_V,
 FW_SCALE,FW_ALPHA,FW_ALARM,FW_RGB,FW_SCALEBONUS,FW_TOTAL
};
static const char*const fwNames[FW_TOTAL]={
 "pixels","time","mypixelx","mypixely","pixelvsp","maintain_shape",
 "def_grav","max_vsp","pixelgravoffset","pixelvspmaxoffset",
 "pixelexpandprog","pixelexpandspd","pixelexpand_h",
 "pixelexpand_v","pixelscale","pixelalpha",
 "pixelspritechangealarm","pixelrgb","scalebonus"
};
static int fwIds[FW_TOTAL];
static VMContext* fwCachedVM=nullptr;
static int fwShineSprite=-1,fwPtcSprite=-1;
static bool fwLogged=false;
static inline double fwArrayGet(GMLArray*a,int i){
 return (double)RValue_toReal(GMLArray_get(a,i));
}
static inline void fwArraySet(GMLArray*a,int i,double v){
 GMLArray_set(a,i,RValue_makeReal((GMLReal)v));
}
static int fwFindSprite(DataWin*dw,const char*name){
 for(uint32_t i=0;i<dw->sprt.count;i++)
  if(dw->sprt.sprites[i].name && strcmp(dw->sprt.sprites[i].name,name)==0)
   return (int)i;
 return -1;
}
static uint32_t fwMergeWhite(uint32_t dst,double t){
 if(t>=1.0)return dst;
 if(t<=0.0)return 0xffffffU;
 uint32_t out=0;
 for(unsigned shift=0;shift<=16;shift+=8){
  double c=(double)((dst>>shift)&255U);
  unsigned n=(unsigned)(255.0+(c-255.0)*t+0.5);
  if(n>255U)n=255U;
  out|=n<<shift;
 }
 return out;
}
static bool vitaTryNativeCh2Fireworks(Runner*runner,Instance*inst,
                                       int32_t codeId){
 if(!runner || !inst || !runner->currentRoom ||
    !runner->currentRoom->name || g_vitaActiveChapter!=2 ||
    codeId<0 || (uint32_t)codeId>=runner->dataWin->code.count)
    return false;
 const char*fwRoom=runner->currentRoom->name;
 /* v0.81-15: the boss slide had no creation thinning and ran at ~16 FPS
  * (Draw 18 ms + Step 16 ms in GML on the Vita). There every second particle
  * is drawn (all of them still move). The other firework rooms already keep
  * only 1/2 or 1/3 of the explosions, so they draw every particle. */
 const bool fwBossSlide=strcmp(fwRoom,"room_dw_cyber_post_music_boss_slide")==0;
 if(!fwBossSlide &&
    strcmp(fwRoom,"room_dw_cyber_maze_fireworks")!=0 &&
    strcmp(fwRoom,"room_dw_city_postbaseball_1")!=0 &&
    strcmp(fwRoom,"room_dw_city_postbaseball_2")!=0)
    return false;
 const int fwDrawStride=fwBossSlide?2:1;
 const char*codeName=runner->dataWin->code.entries[codeId].name;
 if(!codeName)return false;
 bool step=strcmp(codeName,
   "gml_Object_obj_firework_explosion_Step_0")==0;
 bool draw=!step && strcmp(codeName,
   "gml_Object_obj_firework_explosion_Draw_0")==0;
 if(!step && !draw)return false;
 VMContext*vm=runner->vmContext;
 if(!vm)return false;
 if(fwCachedVM!=vm){
  for(int i=0;i<FW_TOTAL;i++)
   fwIds[i]=VM_getOrAllocateVarID(vm,fwNames[i]);
  fwCachedVM=vm;fwShineSprite=-1;fwPtcSprite=-1;
 }
 const int count=RValue_toInt32(Instance_getSelfVar(inst,fwIds[FW_PIXELS]));
 if(count<=0 || count>4096)return false;
 /* Do not write through an aliased GML array: native code does not
  * implement the VM's per-scope copy-on-write owner token. */
 int requiredStep[]={FW_Y,FW_VSP,FW_GRAVOFFSET,FW_MAXOFFSET,
                      FW_PROG,FW_SPD,FW_H,FW_V,FW_SCALE,FW_ALPHA,FW_ALARM};
 int requiredDraw[]={FW_X,FW_Y,FW_H,FW_V,FW_SCALE,FW_ALPHA,FW_ALARM,FW_RGB};
 GMLArray*a[FW_TOTAL]={0};
 int*required=step?requiredStep:requiredDraw;
 unsigned n=step?sizeof(requiredStep)/sizeof(requiredStep[0]):
                 sizeof(requiredDraw)/sizeof(requiredDraw[0]);
 for(unsigned k=0;k<n;k++){
  int id=required[k];
  RValue v=Instance_getSelfVar(inst,fwIds[id]);
  if(v.type!=RVALUE_ARRAY || !v.array ||
     GMLArray_length1D(v.array)<count ||
     (step && v.array->refCount!=1))
      return false;
  a[id]=v.array;
 }
 if(draw && (fwShineSprite<0 || fwPtcSprite<0)){
  fwShineSprite=fwFindSprite(runner->dataWin,"spr_firework_shine");
  fwPtcSprite=fwFindSprite(runner->dataWin,"spr_firework_ptc2");
 }
 if(draw && (fwShineSprite<0 || fwPtcSprite<0 || !runner->renderer))
    return false;
 double time=RValue_toReal(Instance_getSelfVar(inst,fwIds[FW_TIME]));
 if(step){
  bool maintain=RValue_toReal(Instance_getSelfVar(inst,fwIds[FW_MAINTAIN]))!=0;
  double grav=RValue_toReal(Instance_getSelfVar(inst,fwIds[FW_GRAV]));
  double maxVsp=RValue_toReal(Instance_getSelfVar(inst,fwIds[FW_MAXVSP]));
  time+=1.0;
  Instance_setSelfVar(inst,fwIds[FW_TIME],RValue_makeReal((GMLReal)time));
  for(int i=0;i<count;i++){
   double vs=fwArrayGet(a[FW_VSP],i);
   fwArraySet(a[FW_Y],i,fwArrayGet(a[FW_Y],i)+vs);
   vs+=maintain?grav:fwArrayGet(a[FW_GRAVOFFSET],i);
   double limit=maxVsp+(maintain?0.0:fwArrayGet(a[FW_MAXOFFSET],i));
   if(vs>limit)vs=limit;
   fwArraySet(a[FW_VSP],i,vs);
   double prog=fwArrayGet(a[FW_PROG],i),spd=fwArrayGet(a[FW_SPD],i);
   if(prog<1.0){
    prog+=spd;if(prog>=1.0)prog=1.0;
    fwArraySet(a[FW_PROG],i,prog);
    double h=sin(1.5707963267948966*prog);
    fwArraySet(a[FW_H],i,h);
    fwArraySet(a[FW_V],i,sin(1.0995574287564276*h));
   }else{
    double delta=0.01+spd*0.01;
    fwArraySet(a[FW_V],i,fwArrayGet(a[FW_V],i)+delta);
    fwArraySet(a[FW_H],i,fwArrayGet(a[FW_H],i)+delta);
    if(vs>=1.0){
     double scale=fwArrayGet(a[FW_SCALE],i)-0.02;
     if(scale<=0.0)scale=0.0;
     fwArraySet(a[FW_SCALE],i,scale);
    }
   }
   if(!maintain)
    fwArraySet(a[FW_ALPHA],i,fwArrayGet(a[FW_ALPHA],i)-0.01);
   double alarm=fwArrayGet(a[FW_ALARM],i);
   if(alarm!=0.0)fwArraySet(a[FW_ALARM],i,alarm-1.0);
  }
 }else{
  Renderer*renderer=runner->renderer;
  renderer->vtable->gpuSetBlendMode(renderer,1); /* bm_add */
  double factor=fmin(time/30.0,1.0);
  /* GML leaves scalebonus at the value of the last particle. */
  double lastScaleBonus=fwArrayGet(a[FW_ALARM],count-1)==0.0?1.0:0.0;
  for(int i=0;i<count;i+=fwDrawStride){
   bool shine=fwArrayGet(a[FW_ALARM],i)==0.0;
   double bonus=shine?1.0:0.0;
   double scale=fwArrayGet(a[FW_SCALE],i)+bonus;
   double alpha=fwArrayGet(a[FW_ALPHA],i);
   if(scale==0.0 || alpha<=0.0)continue;
   float x=(float)(inst->x+fwArrayGet(a[FW_X],i)*fwArrayGet(a[FW_H],i));
   float y=(float)(inst->y+fwArrayGet(a[FW_Y],i)*fwArrayGet(a[FW_V],i));
   int spr=shine?fwShineSprite:fwPtcSprite;
   int sub=shine?(int)fmod((double)i+0.3*time,4.0):0;
   uint32_t col=fwMergeWhite((uint32_t)fwArrayGet(a[FW_RGB],i),factor);
   Renderer_drawSpriteExt(renderer,spr,sub,x,y,(float)scale,
             (float)scale,0.0f,col,(float)alpha);
  }
  Instance_setSelfVar(inst,fwIds[FW_SCALEBONUS],
                      RValue_makeReal((GMLReal)lastScaleBonus));
  renderer->vtable->gpuSetBlendMode(renderer,0); /* bm_normal */
 }
 if(!fwLogged){
  fwLogged=true;
  char fwLine[160];
  snprintf(fwLine,sizeof(fwLine),
   "VITAGL_CH2_FIREWORK_NATIVE=active step_draw=1 cow_fallback=1 room=%s draw_stride=%d",
   fwRoom,fwDrawStride);
  VitaProbe_logLine(fwLine);
 }
 return true;
}
#endif /* PLATFORM_VITA */
