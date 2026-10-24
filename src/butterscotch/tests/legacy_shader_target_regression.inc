// A shader stays active across surface_set_target in GameMaker. Its projection
// must follow the target, even when shader_set is not called a second time.
static int testLegacyShaderTarget(Renderer* r) {
    RendererVtable* v=r->vtable;
    int shader=-1;
    for(uint32_t i=0;i<r->dataWin->shdr.count;i++)
        if(strcmp(r->dataWin->shdr.shaders[i].name,"shd_pal_swapper")==0) shader=i;
    if(shader<0) return EXIT_FAILURE;
    int src=v->createSurface(r,32,32), dst=v->createSurface(r,8,8);
    int palette=v->createSurface(r,2,128);
    v->gpuResetShader(r);
    v->setRenderTarget(r,palette,false);
    v->drawRectangle(r,0,0,2,128,0,1,false);
    v->drawRectangle(r,0,100,1,101,0xFFFFFF,1,false);
    v->drawRectangle(r,1,100,2,101,0x00FF00,1,false);
    v->setRenderTarget(r,src,false);
    v->drawRectangle(r,0,0,32,32,0xFFFFFF,1,false);
    v->gpuSetShader(r,shader);
    int sampler=v->shaderGetSamplerIndex(r,shader,"u_palTexture");
    v->textureSetStage(r,sampler,v->surfaceGetTexture(r,palette));
    v->shaderSetUniformF(r,v->shaderGetUniform(r,shader,"u_Uvs"),4,0.25f,0.5f/128,1,1);
    v->shaderSetUniformF(r,v->shaderGetUniform(r,shader,"u_pixelSize"),2,0.5f,1.0f/128,0,0);
    v->shaderSetUniformF(r,v->shaderGetUniform(r,shader,"u_paletteId"),1,1,0,0,0);
    v->setRenderTarget(r,dst,false);
    v->drawSurface(r,src,0,0,32,32,0,0,0.25f,0.25f,0,0xFFFFFF,1);
    if(v->flush) v->flush(r);
    uint8_t pixels[8*8*4]={0};
    bool ok=v->surfaceGetPixels(r,dst,pixels);
    int p=(4*8+4)*4;
    ok=ok && pixels[p]<10 && pixels[p+1]>240 && pixels[p+2]<10;
    fprintf(stderr,"LEGACY_SHADER_TARGET=%s center=%u,%u,%u\n",ok?"pass":"FAIL",pixels[p],pixels[p+1],pixels[p+2]);
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
