#ifndef VITA_VIDEO_DRAW_GL_H
#define VITA_VIDEO_DRAW_GL_H

// Shared with the desktop GL regression; decoder/platform state stays in vita_video.c.
static void VitaVideo_drawGL(GLuint frameTexture, GLuint movieProgram, bool firstLoad,
                             float x, float y, float w, float h) {
    // The movie shader consumes clip-space positions. Convert GameMaker's
    // current viewport rectangle here rather than relying on the active room
    // projection, which may be an application-surface or border projection.
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float vw = viewport[2] > 0 ? (float)viewport[2] : 640.0f;
    float vh = viewport[3] > 0 ? (float)viewport[3] : 480.0f;
    float left   = (x / vw) * 2.0f - 1.0f;
    float right  = ((x + w) / vw) * 2.0f - 1.0f;
    float top    = 1.0f - (y / vh) * 2.0f;
    float bottom = 1.0f - ((y + h) / vh) * 2.0f;
    // Match YoYo Loader's triangle-strip orientation exactly. SceAvPlayer's
    // YVU420 texture is top-origin; pairing TOP vertices with V=0 avoids the
    // vertically reversed movie seen in the first-load integration test.
    const float positions[8] = {
        left,  top,
        left,  bottom,
        right, top,
        right, bottom
    };
    const float texcoords_first_load[8] = {
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    };
    // YoYo Loader's movie path uses V=1 on the top vertices. GameMaker's
    // video surface needs that orientation, while the host first_load overlay
    // was already validated with the opposite mapping.
    const float texcoords_yoyo[8] = {
        0.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        1.0f, 0.0f
    };
    const float* texcoords = firstLoad ? texcoords_first_load : texcoords_yoyo;

    // Modern GL intentionally leaves texture unit 1 active after normal GameMaker
    // batches.  The movie shader samples unit 0, so binding without switching
    // units makes the video sample whichever texture GameMaker left on unit 0
    // (commonly the current console border). Preserve the surrounding renderer
    // state because this draw happens in the middle of a GML Draw event.
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture2D = 0;
    GLint previousProgram = 0;
    GLint previousArrayBuffer = 0;
    GLint previousElementArrayBuffer = 0;
    struct {
        GLint enabled, size, type, normalized, stride, buffer;
        void* pointer;
    } attributes[2];
    for (GLuint i = 0; i < 2; ++i) {
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attributes[i].enabled);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attributes[i].size);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attributes[i].type);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attributes[i].normalized);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attributes[i].stride);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attributes[i].buffer);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attributes[i].pointer);
    }
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementArrayBuffer);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);
    GLboolean textureWasEnabled = glIsEnabled(GL_TEXTURE_2D);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D, frameTexture);
    glUseProgram(movieProgram);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    for (GLuint i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)attributes[i].buffer);
        glVertexAttribPointer(i, attributes[i].size, (GLenum)attributes[i].type,
                             (GLboolean)attributes[i].normalized, attributes[i].stride, attributes[i].pointer);
        if (attributes[i].enabled) glEnableVertexAttribArray(i);
        else glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previousArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)previousElementArrayBuffer);
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture2D);
    if (textureWasEnabled) glEnable(GL_TEXTURE_2D);
    else glDisable(GL_TEXTURE_2D);
    glUseProgram((GLuint)previousProgram);
    if (blendWasEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glActiveTexture((GLenum)previousActiveTexture);

}
#endif
