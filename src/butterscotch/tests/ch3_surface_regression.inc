// Opt-in desktop regression through the same vtable used by GML photography.
static int testChapter3Surfaces(Renderer* r) {
    RendererVtable* v = r->vtable;
    v->gpuResetShader(r);
    int src = v->createSurface(r, 32, 32);
    int photo = v->createSurface(r, 8, 8);
    int gray = v->createSurface(r, 8, 8);
    v->setRenderTarget(r, src, false);
    v->setGuiProjection(r, 32, 32, 32, 32, true);
    v->drawRectangle(r, 0, 0, 32, 32, 0x0000FF, 1, false);
    v->drawRectangle(r, 4, 4, 12, 12, 0x996633, 1, false);
    if (v->flush) v->flush(r);
    v->surfaceCopy(r, photo, 0, 0, src, 4, 4, 8, 8, true);
    uint8_t pixels[8 * 8 * 4] = {0};
    bool ok = v->surfaceGetPixels(r, photo, pixels);
    int p = (4 * 8 + 4) * 4;
    ok = ok && pixels[p] == 51 && pixels[p+1] == 102 && pixels[p+2] == 153;
    fprintf(stderr, "CH3_PHOTO_CROP=%s pixel=%u,%u,%u\n", ok ? "pass" : "FAIL", pixels[p], pixels[p+1], pixels[p+2]);
    int shader = -1;
    for (uint32_t i = 0; i < r->dataWin->shdr.count; ++i)
        if (r->dataWin->shdr.shaders[i].present &&
            strcmp(r->dataWin->shdr.shaders[i].name, "shd_grayscalesand") == 0) shader = i;
    if (shader < 0) return EXIT_FAILURE;
    v->setRenderTarget(r, gray, false);
    v->setGuiProjection(r, 8, 8, 8, 8, true);
    v->gpuSetShader(r, shader);
    v->drawSurface(r, photo, 0, 0, 8, 8, 0, 0, 1, 1, 0, 0xFFFFFF, 1);
    if (v->flush) v->flush(r);
    bool grayOk = v->surfaceGetPixels(r, gray, pixels);
    grayOk = grayOk && pixels[p] >= 91 && pixels[p] <= 93 &&
             pixels[p] == pixels[p+1] && pixels[p] == pixels[p+2];
    fprintf(stderr, "CH3_GRAY_SHADER=%s pixel=%u,%u,%u\n", grayOk ? "pass" : "FAIL", pixels[p], pixels[p+1], pixels[p+2]);
    // This dedicated test exits immediately; the OS releases its GL context.
    return ok && grayOk ? EXIT_SUCCESS : EXIT_FAILURE;
}
