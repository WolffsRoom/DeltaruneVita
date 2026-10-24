/* Mirrors touch interaction from the playable Vita port. */
#include <psp2/touch.h>
static int g_settingsTouchHeld=0;
static float g_settingsTouchLastY=0;
static uint32_t g_gameTouchButtons=0;
static void nativeTouchPoll(VitaSettings* settings,SceCtrlData* pad,AudioSystem* audio){
    SceTouchData touch={0};
    int touching=sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1)>0&&touch.reportNum>0;
    g_gameTouchButtons=0;
    if(settings->open){
        if(touching){
            float tx=(float)touch.report[0].x*.5f;
            float ty=(float)touch.report[0].y*.5f;
            if(settings->trophiesMenuOpen){
                if(!g_settingsTouchHeld){
                    g_settingsTouchLastY=ty;
                    if(ty>=190&&ty<=420){
                        int row=(int)((ty-190)/72);
                        int index=settings->trophiesScroll+row;
                        if(row>=0&&row<3&&index<30)settings->trophiesSelected=index;
                    }
                }else{
                    float d=ty-g_settingsTouchLastY;
                    if(d<=-34){settings->trophiesSelected=(settings->trophiesSelected+1)%30;g_settingsTouchLastY=ty;}
                    else if(d>=34){settings->trophiesSelected=(settings->trophiesSelected+29)%30;g_settingsTouchLastY=ty;}
                }
            }else if(settings->controlEditMode){
                if(!g_settingsTouchHeld){
                    int best=0,closest=0x7fffffff;
                    for(int i=0;i<4;i++){
                        int dx=(int)tx-settings->touchControlX[i];
                        int dy=(int)ty-settings->touchControlY[i];
                        int d=dx*dx+dy*dy;
                        if(d<closest){best=i;closest=d;}
                    }
                    settings->selectedTouchControl=best;
                }
                settings->touchControlX[settings->selectedTouchControl]=(int)tx;
                settings->touchControlY[settings->selectedTouchControl]=(int)ty;
            }else if(!g_settingsTouchHeld&&ty>=118&&ty<=166&&tx>=120&&tx<=840){
                int count=settings->devMode?5:4;
                float spacing=count==5?140.0f:180.0f;
                float start=480.0f-spacing*(count-1)*.5f;
                int cat=(int)(((tx-start)/spacing)+.5f);
                if(cat<0)cat=0;
                if(cat>=count)cat=count-1;
                settings->category=cat;settings->selected=0;
            }else if(tx>=180&&tx<=780&&ty>=168&&ty<=420){
                int count=VitaSettings_itemCount(settings->category);
                float first=count==5?184:count==4?200:count==3?206:214;
                float spacing=count==5?38:count==4?42:count==3?50:64;
                int item=(int)((ty-first+spacing*.5f)/spacing);
                if(item>=0&&item<count){
                    settings->selected=item;
                    int cat=settings->category+(!settings->devMode?1:0);
                    int dataIndex=cat==2&&item>=3&&settings->consoleBorderMode!=1?item+1:item;
                    if((cat==3&&dataIndex<3)||(cat==2&&dataIndex==4))
                        VitaSettings_setSliderFromTouch(settings,audio,cat,dataIndex,tx);
                    else if(!g_settingsTouchHeld)pad->buttons|=SCE_CTRL_CROSS;
                }
            }
        }
        g_settingsTouchHeld=touching;
        VitaSettings_setTouchVisuals(settings,0,0,false,false,false);
        return;
    }
    g_settingsTouchHeld=0;
    float ax=0,ay=0;
    bool confirm=false,cancel=false,menu=false;
    if(touching&&settings->touchEnabled){
        for(unsigned n=0;n<touch.reportNum;n++){
            int tx=touch.report[n].x,ty=touch.report[n].y;
            int dx=tx-settings->touchControlX[0]*2;
            int dy=ty-settings->touchControlY[0]*2;
            int rad=210*settings->touchControlScale[0]/100;
            if(rad>0&&dx*dx+dy*dy<=(rad*3)*(rad*3)){
                int cx=dx<-rad?-rad:dx>rad?rad:dx;
                int cy=dy<-rad?-rad:dy>rad?rad:dy;
                ax=(float)cx/rad;ay=(float)cy/rad;
                if(abs(cx)>abs(cy)){
                    if(cx<-(rad*3/7))g_gameTouchButtons|=SCE_CTRL_LEFT;
                    if(cx>(rad*3/7))g_gameTouchButtons|=SCE_CTRL_RIGHT;
                }else{
                    if(cy<-(rad*3/7))g_gameTouchButtons|=SCE_CTRL_UP;
                    if(cy>(rad*3/7))g_gameTouchButtons|=SCE_CTRL_DOWN;
                }
            }else{
                int best=-1,dist=0x7fffffff;
                for(int i=0;i<3;i++){
                    int ex=tx-settings->touchControlX[i+1]*2;
                    int ey=ty-settings->touchControlY[i+1]*2;
                    int r=180*settings->touchControlScale[i+1]/100;
                    int d=ex*ex+ey*ey;
                    if(d<=r*r&&d<dist){best=i;dist=d;}
                }
                if(best==0)confirm=true;
                else if(best==1)cancel=true;
                else if(best==2)menu=true;
            }
        }
    }
    if(confirm)g_gameTouchButtons|=SCE_CTRL_CROSS;
    if(cancel)g_gameTouchButtons|=SCE_CTRL_CIRCLE;
    if(menu)g_gameTouchButtons|=SCE_CTRL_TRIANGLE;
    float px=(float)((int)pad->lx-128)/127.f;
    float py=(float)((int)pad->ly-128)/127.f;
    VitaSettings_setTouchVisuals(settings,
        fabsf(px)>.2f?px:ax,fabsf(py)>.2f?py:ay,
        (pad->buttons&SCE_CTRL_CROSS)!=0||confirm,
        (pad->buttons&(SCE_CTRL_CIRCLE|SCE_CTRL_SQUARE))!=0||cancel,
        (pad->buttons&SCE_CTRL_TRIANGLE)!=0||menu);
}
