/* Chapter0 prologue: real UTF-8 strings drawn from a glyph atlas generated
 * with the user's local undertale-deltarune-extended-fixed.otf.
 * The font file itself is NEVER shipped. Japanese/Cyrillic glyphs use fallback
 * only when absent from original. Font copyright Viika, FontStruct.
 * https://fontstruct.com/fontstructions/show/2008226 */
#include "vita_prologue_glyphs.h"
static int egg_live_atlas=0;
static int egg_live_cross_icon=0;
/* Used only while drawing the dialogue panel. The source glyph atlas stores
 * 29px characters; JP/RU strings can span far more than Latin script. */
static float egg_live_dialog_scale=1.0f;
static unsigned egg_live_next(const unsigned char**p){
    unsigned c=*(*p)++;
    if(c<128)return c;
    unsigned k=0;
    if((c&0xe0)==0xc0)k=1,c&=0x1f;
    else if((c&0xf0)==0xe0)k=2,c&=0x0f;
    else if((c&0xf8)==0xf0)k=3,c&=0x07;
    else return '?';
    for(unsigned i=0;i<k;i++){
        unsigned b=**p;
        if((b&0xc0)!=0x80)return '?';
        (*p)++;c=(c<<6)|(b&63);
    }
    return c;
}
static unsigned egg_live_count(const char*s){
    unsigned n=0;const unsigned char*p=(const unsigned char*)s;
    if(!p)return 0;
    while(*p){egg_live_next(&p);n++;}
    return n;
}
static const EggGlyph*egg_live_glyph(unsigned cp){
    int lo=0,hi=(int)(sizeof(egg_glyph_map)/sizeof(egg_glyph_map[0]))-1;
    while(lo<=hi){
        int i=(lo+hi)/2;
        if(egg_glyph_map[i].cp==cp)return &egg_glyph_map[i];
        if(egg_glyph_map[i].cp<cp)lo=i+1;else hi=i-1;
    }
    return NULL;
}
static float egg_live_width(const char*text){
    float width=0;const unsigned char*p=(const unsigned char*)text;
    if(!p)return 0;
    while(*p){
        unsigned cp=egg_live_next(&p);
        const EggGlyph*g=egg_live_glyph(cp);
        width+=g?g->adv:14;
    }
    return width;
}
static void egg_live_batch(NVGcontext*vg,LoadingVertex*v,int n,
                            int red,int green,int blue,float alpha){
    if(n==0||egg_live_atlas<=0||egg_live_atlas>vg->n)return;
    GLuint texture=vg->textures[egg_live_atlas];
    if(!texture)return;
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,texture);
    glColor4f(red/255.0f,green/255.0f,blue/255.0f,alpha);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2,GL_FLOAT,sizeof(LoadingVertex),&v[0].x);
    glTexCoordPointer(2,GL_FLOAT,sizeof(LoadingVertex),&v[0].u);
    glDrawArrays(GL_TRIANGLES,0,n);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
}
static void egg_live_cross(NVGcontext*vg,float x,float y,
                            int red,int green,int blue,float alpha){
    (void)red;(void)green;(void)blue;
    /* Exact sprite supplied in data/addons/ui/button_ps4_cross_0.png.
     * Original dimensions 12x14, scaled without changing its ratio. */
    if(egg_live_cross_icon>0)
        egg_image(vg,egg_live_cross_icon,x,y,22,26,alpha);
}
/* Draw whole literal Unicode text at runtime; never crop phrase screenshots.
 * Each glyph is an individual quad, assembled into one OpenGL draw batch.
 * The PSVita Cross glyph replaces standalone ASCII X in instructions.
 *
 * v0.82: the Japanese (kind 1) and Cyrillic (kind 2) glyphs come from a
 * fallback font whose bearings are relative to a different baseline: a
 * Cyrillic capital ended 17 px above a Latin capital, so Russian lines rose
 * into the box border and mixed words ("PS Vita", "Wolff") sat lower than
 * the rest. Their quads are now shifted onto the Latin baseline. */
static float egg_live_kind_shift(const EggGlyph*g){
    if(!g)return 0.0f;
    return g->kind==2?17.0f:(g->kind==1?13.0f:0.0f);
}
static int egg_live_is_cross(int crossIcon,const unsigned char*start,
                             const unsigned char*begin,const unsigned char*next,
                             unsigned cp){
    return crossIcon && cp=='X' &&
        (start==begin||start[-1]==' '||start[-1]=='/'||start[-1]=='(') &&
        (*next==0||*next==' '||*next=='.'||*next==',');
}
/* Lays out (and, if emit, draws) the text. Returns the bottom of the last
 * line (pen y plus the 29 px glyph cell, scaled), so callers can fit text. */
static float egg_live_layout(NVGcontext*vg,const char*str,float x,float y,
 float maxWidth,unsigned frame,int visible,int red,int green,int blue,
 float alpha,int wave,int crossIcon,int emit){
    if(!str)return y;
    LoadingVertex v[2200];int n=0;
    const unsigned char*p=(const unsigned char*)str;
    const unsigned char*begin=p;
    float penX=x,penY=y;
    const float glyphScale=egg_live_dialog_scale;
    unsigned letter=0;
    while(*p&&(visible<0||(int)letter<visible)){
        const unsigned char*start=p;
        unsigned cp=egg_live_next(&p);
        letter++;
        if(cp=='\n'){penX=x;penY+=37.0f*glyphScale;continue;}
        int cross=egg_live_is_cross(crossIcon,start,begin,p,cp);
        const EggGlyph*g=egg_live_glyph(cp);
        if(!g)g=egg_live_glyph('?');
        float advance=(cross?27:(g?g->adv:14))*glyphScale;
        /* Wrap before the NEXT complete word, never inside it. This also
         * prevents leaving a space at the start of the new line. */
        if(cp==' '){
            float nextWidth=0;
            const unsigned char*look=p;
            while(*look && *look!=' ' && *look!='\n'){
                const unsigned char*startLook=look;
                unsigned nextCp=egg_live_next(&look);
                const EggGlyph*nextGlyph=egg_live_glyph(nextCp);
                int nextCross=egg_live_is_cross(crossIcon,startLook,begin,look,nextCp);
                nextWidth+=(nextCross?27:(nextGlyph?nextGlyph->adv:14))*glyphScale;
            }
            if(penX>x && penX+advance+nextWidth>x+maxWidth){
                penX=x;penY+=37.0f*glyphScale;continue;
            }
        }
        if(penX>x && penX+advance>x+maxWidth){penX=x;penY+=37.0f*glyphScale;}
        if(emit){
            float shift=wave?sinf(frame*.13f+letter*.37f)*1.25f:0;
            if(cross){
                egg_live_batch(vg,v,n,red,green,blue,alpha);n=0;
                egg_live_cross(vg,penX,penY+shift+5*glyphScale,red,green,blue,alpha);
            }else if(g&&g->w&&g->h){
                if(n+6>2200){egg_live_batch(vg,v,n,red,green,blue,alpha);n=0;}
                float xx=penX+g->bx*glyphScale;
                float yy=penY+(g->by+egg_live_kind_shift(g))*glyphScale+shift;
                float x0=xx/480.0f-1,x1=(xx+g->w*glyphScale)/480.0f-1;
                float y0=1-yy/272.0f,y1=1-(yy+g->h*glyphScale)/272.0f;
                float u0=g->x/1024.0f,u1=(g->x+g->w)/1024.0f;
                float t0=g->y/1024.0f,t1=(g->y+g->h)/1024.0f;
                LoadingVertex quad[6]={
                  {u0,t0,x0,y0},{u1,t0,x1,y0},{u1,t1,x1,y1},
                  {u0,t0,x0,y0},{u1,t1,x1,y1},{u0,t1,x0,y1}};
                for(int i=0;i<6;i++)v[n++]=quad[i];
            }
        }
        penX+=advance;
    }
    if(emit)egg_live_batch(vg,v,n,red,green,blue,alpha);
    return penY+29.0f*glyphScale;
}
static void egg_live_draw(NVGcontext*vg,const char*str,float x,float y,
 float maxWidth,unsigned frame,int visible,int red,int green,int blue,
 float alpha,int wave,int crossIcon){
    if(!str||egg_live_atlas<=0||alpha<=0)return;
    egg_live_layout(vg,str,x,y,maxWidth,frame,visible,red,green,blue,alpha,
                    wave,crossIcon,1);
}
/* Largest scale (1.0 down to 0.56) at which the COMPLETE string fits between
 * y and bottom, so the typewriter never grows past the box. */
static float egg_live_fit_scale(const char*str,float x,float y,float maxWidth,
                                float bottom,int crossIcon){
    float prev=egg_live_dialog_scale,scale=1.0f;
    for(;scale>0.56f;scale-=0.04f){
        egg_live_dialog_scale=scale;
        if(egg_live_layout(NULL,str,x,y,maxWidth,0,-1,0,0,0,1,0,crossIcon,0)<=bottom)
            break;
    }
    egg_live_dialog_scale=prev;
    return scale;
}
