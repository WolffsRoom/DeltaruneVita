// Opt-in desktop readback test, included where the renderer helpers are visible.
bool GLRenderer_testExternalPng(GLRenderer* gl, const char* path) {
    int w, h, channels;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    if (length <= 0 || length > 1024 * 1024) { fclose(file); return false; }
    unsigned char* encoded = safeMalloc(length);
    size_t read = fread(encoded, 1, length, file);
    fclose(file);
    unsigned char* pixels = read == (size_t)length ? stbi_load_from_memory(encoded, length, &w, &h, &channels, 4) : NULL;
    free(encoded);
    if (!pixels) return false;
    flushBatch(gl);
    GLint framebuffer, viewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean blend = glIsEnabled(GL_BLEND);
    int32_t savedShader = gl->base.currentShader;
    Matrix4f matrices[MATRICES_MAX];
    memcpy(matrices, gl->base.gmlMatrices, sizeof(matrices));
    GLuint source, target, fbo;
    glGenTextures(1, &source);
    glBindTexture(GL_TEXTURE_2D, source);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    unsigned char expected[4];
    memcpy(expected, pixels, 4);
    stbi_image_free(pixels);
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    glViewport(0, 0, 8, 8);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    for (int i = 0; i < MATRICES_MAX; ++i) Matrix4f_identity(&gl->base.gmlMatrices[i]);
    // Force a custom black shader. The external image must bypass it and restore it.
    uint32_t index = gl->gmlShaderCount++;
    gl->gmlShaders = safeRealloc(gl->gmlShaders, gl->gmlShaderCount * sizeof(GMLShader));
    memset(&gl->gmlShaders[index], 0, sizeof(GMLShader));
    const char* attrs[] = {"in_Position", "in_Colour", "in_TextureCoord"};
    bool ok = compileProgram(&gl->gmlShaders[index], "external_png_regression",
        "#version 120\nattribute vec3 in_Position; void main(){gl_Position=vec4(in_Position,1.0);}",
        "#version 120\nvoid main(){gl_FragColor=vec4(0.0,0.0,0.0,1.0);}", 3, attrs);
    if (ok) {
        glGpuSetShader(&gl->base, index);
        GLRenderer_drawExternalTexture(gl, source, -1, -1, 2, 2, 1);
        unsigned char actual[4] = {0};
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        ok = memcmp(expected, actual, 4) == 0 && gl->base.currentShader == (int32_t)index && gl->batchCount == 0;
        fprintf(stderr, "LOCAL_TEST_EXTERNAL_PNG=%s pixel=%u,%u,%u,%u\n",
                ok ? "pass" : "FAIL", actual[0], actual[1], actual[2], actual[3]);
        // Distinct quadrants catch ignored corner mirroring; a solid PNG
        // cannot distinguish the four Game Settings border orientations.
        const unsigned char corners[16] = {
            255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,0,255
        };
        glBindTexture(GL_TEXTURE_2D, source);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, corners);
        bool mirrored = true;
        for (int fy = 0; fy < 2; ++fy) for (int fx = 0; fx < 2; ++fx) {
            GLRenderer_drawExternalTextureFlipped(gl, source, -1, -1, 2, 2, 1, fx != 0, fy != 0);
            // Read the visual top-left: GL readback starts at the bottom.
            glReadPixels(2, 6, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
            fprintf(stderr, "LOCAL_TEST_CORNER_SAMPLE flip=%d,%d rgba=%u,%u,%u,%u\n", fx, fy, actual[0], actual[1], actual[2], actual[3]);
            mirrored = mirrored && memcmp(actual, corners + (fy * 2 + fx) * 4, 4) == 0;
            mirrored = mirrored && gl->base.currentShader == (int32_t)index;
        }
        fprintf(stderr, "LOCAL_TEST_CORNER_FLIPS=%s\n", mirrored ? "pass" : "FAIL");
        ok = ok && mirrored;
    }
    glGpuResetShader(&gl->base);
    freeShader(&gl->gmlShaders[index]);
    gl->gmlShaderCount--;
    // Exercise real font atlases, not only an externally decoded PNG. Each
    // eligible font must produce visible pixels through the text draw path.
    GMLCamera savedGui = gl->base.runner->guiCamera;
    int32_t savedFont = gl->base.drawFont;
    int32_t savedHalign = gl->base.drawHalign, savedValign = gl->base.drawValign;
    gl->base.drawHalign = gl->base.drawValign = 0;
    glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glViewport(0, 0, 256, 128);
    glSetGuiProjection(&gl->base, 256, 128, 256, 128, true);
    unsigned char* textPixels = safeMalloc(256 * 128 * 4);
    uint32_t testedFonts = 0;
    for (uint32_t i = 0; i < gl->base.dataWin->font.count; ++i) {
        Font* font = &gl->base.dataWin->font.fonts[i];
        FontGlyph* glyph = font->present ? TextUtils_findGlyph(font, 'A') : NULL;
        if (!font->present || font->isSpriteFont || !glyph || !glyph->sourceWidth || !glyph->sourceHeight) continue;
        gl->base.drawFont = i;
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        drawText(&gl->base, "A", 32, 48, 1, 1, 0, -1, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 1);
        flushBatch(gl);
        glReadPixels(0, 0, 256, 128, GL_RGBA, GL_UNSIGNED_BYTE, textPixels);
        uint32_t visible = 0;
        for (uint32_t p = 0; p < 256 * 128; ++p)
            if (textPixels[p * 4 + 3] && (textPixels[p * 4] || textPixels[p * 4 + 1] || textPixels[p * 4 + 2])) visible++;
        int page = font->tpagIndex >= 0 ? gl->base.dataWin->tpag.items[font->tpagIndex].texturePageId : -1;
        fprintf(stderr, "LOCAL_TEST_FONT=%s font=%u page=%d pixels=%u\n", visible ? "pass" : "FAIL", i, page, visible);
        ok = ok && visible > 0;
        testedFonts++;
    }
    ok = ok && testedFonts > 0;
    free(textPixels);
    // A copy from an offscreen surface must preserve the destination of
    // subsequent game draws. Read back the copied sprite after freeing source.
    int32_t copySurface = glCreateSurface(&gl->base, 32, 32);
    glBindFramebuffer(GL_FRAMEBUFFER, gl->surfaces[copySurface]);
    glViewport(0, 0, 32, 32);
    glSetGuiProjection(&gl->base, 32, 32, 32, 32, true);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    gl->base.vtable->drawRectangle(&gl->base, 4, 4, 12, 12, 0x996633, 1, false);
    flushBatch(gl);
    // A nonuniform source catches vertical crop offsets hidden by a solid
    // colour surface. This is the camera's surface_copy_part -> sprite path.
    int32_t photoSurface = glCreateSurface(&gl->base, 8, 8);
    glSurfaceCopy(&gl->base, photoSurface, 0, 0, copySurface, 4, 4, 8, 8, true);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    int32_t copiedSprite = glCreateSpriteFromSurface(&gl->base, photoSurface, 0, 0, 8, 8, false, false, 0, 0);
    GLint afterCopy = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &afterCopy);
    bool copyOk = copiedSprite >= 0 && afterCopy == (GLint)fbo;
    glSurfaceFree(&gl->base, copySurface);
    glSurfaceFree(&gl->base, photoSurface);
    if (copiedSprite >= 0) {
        Sprite* sprite = &gl->base.dataWin->sprt.sprites[copiedSprite];
        uint32_t page = gl->base.dataWin->tpag.items[sprite->tpagIndices[0]].texturePageId;
        GLuint readFbo;
        glGenFramebuffers(1, &readFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->glTextures[page], 0);
        unsigned char pixel[4];
        glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        copyOk = copyOk && pixel[0] == 51 && pixel[1] == 102 && pixel[2] == 153 && pixel[3] == 255;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDeleteFramebuffers(1, &readFbo);
        glDeleteSprite(&gl->base, copiedSprite);
    }
    fprintf(stderr, "LOCAL_TEST_SURFACE_COPY=%s\n", copyOk ? "pass" : "FAIL");
    ok = ok && copyOk;
    gl->base.drawFont = savedFont;
    gl->base.drawHalign = savedHalign;
    gl->base.drawValign = savedValign;
    gl->base.runner->guiCamera = savedGui;
    memcpy(gl->base.gmlMatrices, matrices, sizeof(matrices));
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (blend) glEnable(GL_BLEND);
    if (savedShader >= 0) glGpuSetShader(&gl->base, savedShader);
    else glShaderSettingsRefresh(&gl->base);
    glDeleteFramebuffers(1, &fbo);
    deleteRendererTextures(gl, 1, &target);
    deleteRendererTextures(gl, 1, &source);
    return ok;
}
