#ifndef DELTARUNE_GXM_RGBA4444_NATIVE_H
#define DELTARUNE_GXM_RGBA4444_NATIVE_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
/* Public image-format flag from nanovg_gxm_native.h; settings.c uses
 * only nanovg.h, so share the same numeric flag here. */
#ifndef NVG_IMAGE_RGBA4444
#define NVG_IMAGE_RGBA4444 (1 << 11)
#endif

/* The Vita3K-tested U4U4U4U4_ABGR native GXM layout expects R,G,B,A
 * in high-to-low nibble order. The source decoder provides RGBA8888
 * (straight/unpremultiplied alpha); NanoVG premultiplies in fragment GXP.
 * Do not swap channels or premultiply here. */
static inline uint16_t gxmRGBA4444Pack(uint8_t r,uint8_t g,uint8_t b,uint8_t a){
    return (uint16_t)(((uint16_t)(r>>4)<<12)|
                      ((uint16_t)(g>>4)<<8)|
                      ((uint16_t)(b>>4)<<4)|
                      (uint16_t)(a>>4));
}
/* Hardware colour-order diagnostics: pixel samples may disagree between
 * Vita3K and retail GXM drivers. Keep variant 0 as the previously screenshot-
 * verified ABGR texture packing; NEVER silently alter the baseline for all
 * users. Alternate 4-bit channel orders are opted in by a file in ux0 and
 * work across atlases, borders, text/icons and dynamic sprites. */
static inline uint16_t gxmRGBA4444PackVariant(
    uint8_t r,uint8_t g,uint8_t b,uint8_t a,int variant){
    const uint8_t q[4]={r,g,b,a};
    static const uint8_t perm[9][4]={
        {0,1,2,3}, /* RGB,A: tested Vita3K baseline */
        {0,2,1,3}, /* RBG,A */
        {1,0,2,3}, /* GRB,A */
        {1,2,0,3}, /* GBR,A */
        {2,0,1,3}, /* BRG,A */
        {2,1,0,3}, /* BGR,A */
        {3,2,1,0}, /* ABGR */
        {3,0,1,2}, /* ARGB */
        {3,1,2,0}  /* Vita HW alpha/red lane correction, inverse swap R<->A */
    };
    const uint8_t *v=perm[variant>=0&&variant<=8?variant:0];
    return (uint16_t)(((uint16_t)(q[v[0]]>>4)<<12)|
                      ((uint16_t)(q[v[1]]>>4)<<8) |
                      ((uint16_t)(q[v[2]]>>4)<<4) |
                      (uint16_t)(q[v[3]]>>4));
}
static inline int gxmRGBA4444Layout(void){
    static int layout=-1;
    if(layout>=0)return layout;
    /* Source data is GL_UNSIGNED_SHORT_4_4_4_4 (0xRGBA).  The native image
     * now uses SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_RGBA, matching VitaGL's
     * proven upload path.  The former ABGR texture plus layout 8 compensated
     * by swapping R/A and made Chapters 1-5 pink on retail hardware. */
    extern int g_vitaActiveChapter;
    layout=0;
    /* Per-chapter user override wins; legacy global override is retained. */
    char path[128];
    snprintf(path,sizeof(path),
      "ux0:data/deltarune/deltarunevita/gxm-color-layout-ch%d.txt",
      g_vitaActiveChapter);
    FILE *f=fopen(path,"rb");
    if(!f)f=fopen("ux0:data/deltarune/deltarunevita/gxm-color-layout.txt","rb");
    if(f){
        int c=fgetc(f);fclose(f);
        if(c>='0'&&c<='8')layout=c-'0';
    }
    return layout;
}
static inline uint16_t* gxmRGBA4444Convert(const uint8_t*rgba,size_t pixels){
    if(!rgba||!pixels||pixels>SIZE_MAX/(sizeof(uint16_t)))return NULL;
    uint16_t*packed=(uint16_t*)malloc(pixels*sizeof(uint16_t));
    if(!packed)return NULL;
    for(size_t i=0;i<pixels;i++){
        const uint8_t*src=rgba+4u*i;
        packed[i]=gxmRGBA4444PackVariant(
            src[0],src[1],src[2],src[3],gxmRGBA4444Layout());
    }
    return packed;
}
static inline int gxmRGBA4444Upload(NVGcontext*vg,int w,int h,
                                     int flags,const uint8_t*rgba){
    if(!vg||w<=0||h<=0||!rgba)return 0;
    const size_t pixels=(size_t)w*(size_t)h;
    if(pixels/(size_t)w!=(size_t)h)return 0;
    uint16_t*packed=gxmRGBA4444Convert(rgba,pixels);
    if(!packed)return 0;
    int image=nvgCreateImageRGBA(vg,w,h,flags|NVG_IMAGE_RGBA4444,
                                  (const uint8_t*)packed);
    free(packed);
    return image;
}
#endif
