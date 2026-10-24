#include "camera_quad.h"
static int testFinaleRender(Renderer* r) {
    RendererVtable* v=r->vtable;
    v->gpuResetShader(r);
    v->gpuSetColorWriteEnable(r,true,true,true,true);
    v->gpuSetAlphaTestEnable(r,false);
    int target=v->createSurface(r,8,8);
    v->setRenderTarget(r,target,false);
    v->setGuiProjection(r,8,8,8,8,true);
    uint8_t reference[8*8*4], restored[8*8*4];
    bool ok=true;
    for(int pass=0;pass<2;pass++) {
        v->gpuSetBlendEnable(r,false);
        v->drawRectangle(r,0,0,8,8,0xFF0000,1,false);
        v->gpuSetBlendEnable(r,true);
        v->gpuSetBlendMode(r,bm_normal);
        BlendFactors factors=v->gpuGetBlendFactors(r);
        ok &= factors.src==bm_src_alpha && factors.dst==bm_inv_src_alpha &&
              factors.srcAlpha==bm_src_alpha && factors.dstAlpha==bm_inv_src_alpha;
        if(pass) v->gpuSetBlendModeExt(r,factors.src,factors.dst,factors.srcAlpha,factors.dstAlpha);
        v->drawRectangle(r,0,0,8,8,0x0000FF,0.5f,false);
        if(v->flush) v->flush(r);
        ok &= v->surfaceGetPixels(r,target,pass?restored:reference);
    }
    ok &= memcmp(reference,restored,sizeof(reference))==0;
    fprintf(stderr,"FINALE_BLEND=%s normal=%u,%u,%u restored=%u,%u,%u\n",ok?"pass":"FAIL",
            reference[0],reference[1],reference[2],restored[0],restored[1],restored[2]);
    // scr_draw_set_mask / scr_draw_in_mask_begin: a rectangle writes only
    // destination alpha; an independent opaque note must stay inside it.
    v->gpuSetBlendEnable(r,false);
    v->gpuSetColorWriteEnable(r,true,true,true,true);
    v->drawRectangle(r,0,0,7,7,0xFF0000,0,false);
    v->gpuSetColorWriteEnable(r,false,false,false,true);
    v->drawRectangle(r,2,2,5,5,0,1,false);
    v->gpuSetColorWriteEnable(r,true,true,true,true);
    v->gpuSetBlendEnable(r,true);
    v->gpuSetBlendModeExt(r,bm_dest_alpha,bm_inv_dest_alpha,bm_dest_alpha,bm_inv_dest_alpha);
    v->gpuSetAlphaTestEnable(r,true);
    v->gpuSetAlphaTestRef(r,1);
    v->drawRectangle(r,0,0,7,7,0x0000FF,1,false);
    if(v->flush) v->flush(r);
    bool mask=v->surfaceGetPixels(r,target,restored);
    int middle=(3*8+3)*4;
    mask &= restored[0]==0 && restored[2]==255 && restored[middle]==255 && restored[middle+2]==0;
    v->gpuSetAlphaTestEnable(r,false);
    v->gpuSetAlphaTestRef(r,0);
    v->gpuSetBlendMode(r,bm_normal);
    fprintf(stderr,"FINALE_MASK=%s\n",mask?"pass":"FAIL");
    const char* assets[]={"spr_rhythmgame_note","spr_rk_quickslash_marker","spr_shelter_open"};
    int spriteTarget=v->createSurface(r,128,128);
    v->setRenderTarget(r,spriteTarget,false);
    v->setGuiProjection(r,128,128,128,128,true);
    uint8_t* image=safeMalloc(128*128*4);
    for(int a=0;a<3;a++) {
        Sprite* sprite=nullptr;
        for(uint32_t s=0;s<r->dataWin->sprt.count;s++)
            if(r->dataWin->sprt.sprites[s].name && !strcmp(r->dataWin->sprt.sprites[s].name,assets[a]))
                sprite=&r->dataWin->sprt.sprites[s];
        if(!sprite || !sprite->textureCount) { ok=false; continue; }
        v->gpuSetBlendEnable(r,false);
        v->drawRectangle(r,0,0,127,127,0,0,false);
        v->gpuSetColorWriteEnable(r,false,false,false,true);
        v->drawRectangle(r,8,8,119,119,0,1,false);
        v->gpuSetColorWriteEnable(r,true,true,true,true);
        v->gpuSetBlendEnable(r,true);
        v->gpuSetBlendModeExt(r,bm_dest_alpha,bm_inv_dest_alpha,bm_dest_alpha,bm_inv_dest_alpha);
        v->gpuSetAlphaTestEnable(r,true);
        v->gpuSetAlphaTestRef(r,1);
        float scale=fminf(1.0f,110.0f/fmaxf(sprite->width,sprite->height));
        v->drawSprite(r,sprite->tpagIndices[0],9,9,0,0,scale,scale,0,0xFFFFFF,1);
        if(v->flush) v->flush(r);
        bool read=v->surfaceGetPixels(r,spriteTarget,image);
        int visible=0;
        for(int p=0;p<128*128;p++) if(image[p*4]||image[p*4+1]||image[p*4+2]) visible++;
        ok &= read && visible>0;
        fprintf(stderr,"FINALE_ASSET name=%s visible_pixels=%d result=%s\n",assets[a],visible,read&&visible>0?"pass":"FAIL");
        v->gpuSetAlphaTestEnable(r,false);
    }
    free(image);
    Matrix4f clip;
    Matrix4f_identity(&clip);
    // The actual view puts this high-world-coordinate bunker/attack on screen
    // even if camera.viewY still points at the old part of the room.
    clip.m[0]=2.0f/640; clip.m[5]=2.0f/480;
    clip.m[12]=-1; clip.m[13]=-1-1000*clip.m[5];
    const float on[8]={100,1100,200,1100,200,1200,100,1200};
    const float off[8]={100,0,200,0,200,100,100,100};
    bool projection=CameraQuad_classify(&clip,on,.4f,.4f)==2 &&
                    CameraQuad_classify(&clip,off,.4f,.4f)==0;
    clip.m[3]=.1f;
    projection &= CameraQuad_classify(&clip,off,.4f,.4f)==2;
    fprintf(stderr,"FINALE_PROJECTION=%s\n",projection?"pass":"FAIL");
    return ok && mask && projection?EXIT_SUCCESS:EXIT_FAILURE;
}
