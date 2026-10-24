static int testCh5Shadow(Renderer* r) {
    RendererVtable* v = r->vtable;
    int shader = -1;
    for (uint32_t i = 0; i < r->dataWin->shdr.count; i++)
        if (r->dataWin->shdr.shaders[i].name &&
            strcmp(r->dataWin->shdr.shaders[i].name, "shd_shadowblend") == 0)
            shader = (int)i;
    if (shader < 0) return EXIT_FAILURE;
    int mask = v->createSurface(r, 16, 16);
    int result = v->createSurface(r, 16, 16);
    v->gpuResetShader(r);
    v->setRenderTarget(r, mask, false);
    v->setGuiProjection(r,16,16,16,16,true);
    v->gpuSetBlendEnable(r, false);
    v->drawRectangle(r,0,0,15,15,0xFFFFFF,0,false);
    v->drawRectangle(r,4,4,11,11,0xFFFFFF,1,false);
    if(v->flush) v->flush(r);
    uint8_t maskPixels[16*16*4]={0};
    bool maskOk=v->surfaceGetPixels(r,mask,maskPixels);
    v->setRenderTarget(r, result, false);
    v->setGuiProjection(r,16,16,16,16,true);
    v->drawRectangle(r,0,0,15,15,0xFFFFFF,1,false);
    v->gpuSetBlendEnable(r, true);
    v->gpuSetBlendMode(r,bm_normal);
    v->gpuSetShader(r,shader);
    int uniform=v->shaderGetUniform(r,shader,"shadowCol");
    v->shaderSetUniformF(r,uniform,4,0.1f,0.2f,0.3f,0.5f);
    v->drawSurface(r,mask,0,0,16,16,0,0,1,1,0,0xFFFFFF,1);
    v->gpuResetShader(r);
    if(v->flush) v->flush(r);
    uint8_t pixels[16*16*4]={0};
    bool ok=v->surfaceGetPixels(r,result,pixels);
    int outside=(2*16+2)*4, inside=(8*16+8)*4;
    ok &= maskOk && maskPixels[inside+3]>245 && maskPixels[outside+3]<10;
    ok &= pixels[outside]>245 && pixels[outside+1]>245 && pixels[outside+2]>245;
    ok &= pixels[inside]>120 && pixels[inside]<155 &&
          pixels[inside+1]>135 && pixels[inside+1]<170 &&
          pixels[inside+2]>145 && pixels[inside+2]<185;
    fprintf(stderr,"CH5_SHADOW=%s uniform=%d mask=%u/%u outside=%u,%u,%u inside=%u,%u,%u,%u\n",
            ok?"pass":"FAIL",uniform,maskPixels[outside+3],maskPixels[inside+3],pixels[outside],pixels[outside+1],pixels[outside+2],
            pixels[inside],pixels[inside+1],pixels[inside+2],pixels[inside+3]);
    return ok?EXIT_SUCCESS:EXIT_FAILURE;
}
