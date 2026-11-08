/* Low-cost CRT Mod: multiply the scene by a scanline, grille and vignette pattern.
 * No framebuffer capture, no extra render target/texture, no UV warp/zoom.
 * Runs before GML/native GUI; the scene beneath is never read back. */
static GLuint vitaCrtModProgram=0;
static int vitaCrtModDisabled=0;
static int vitaCrtModGuardState=0;
#define VITA_CRT_MOD_PENDING "ux0:data/deltarune/deltarunevita/crt_mod_boot_pending"
static const GLfloat vitaCrtOverlayVertices[]={
    -1.0f,-1.0f,0.0f,0.0f,
     1.0f,-1.0f,1.0f,0.0f,
    -1.0f, 1.0f,0.0f,1.0f,
     1.0f, 1.0f,1.0f,1.0f
};
static GLuint vitaCrtModCompile(GLenum type,const char* code){
    GLuint shader=glCreateShader(type);
    if(!shader)return 0;
    glShaderSource(shader,1,&code,NULL);
    log_line(type==GL_VERTEX_SHADER?"VITAGL_CRT_MOD=vertex_compile_begin":"VITAGL_CRT_MOD=fragment_compile_begin");
    glCompileShader(shader);
    GLint success=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
    if(!success){
        char err[256]={0};glGetShaderInfoLog(shader,255,NULL,err);
        log_line(err);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
static bool vita_crt_mod_init(void){
    if(vitaCrtModDisabled)return false;
    if(vitaCrtModProgram)return true;
    if(!vitaCrtModGuardState){
        SceIoStat st;
        if(sceIoGetstat(VITA_CRT_MOD_PENDING,&st)>=0){
            sceIoRemove(VITA_CRT_MOD_PENDING);
            vitaCrtModDisabled=1;
            vitaCrtModGuardState=3;
            log_line("VITAGL_CRT_MOD=prior_crash_safe_fallback");
            return false;
        }
        SceUID fd=sceIoOpen(VITA_CRT_MOD_PENDING,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);
        if(fd>=0){
            const char* msg="crt overlay initialization";
            sceIoWrite(fd,msg,(unsigned)strlen(msg));sceIoClose(fd);
        }
        vitaCrtModGuardState=1;
    }
    static const char* vertex=
        "attribute vec2 a_position; attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main(){v_uv=a_uv;gl_Position=vec4(a_position,0.0,1.0);}\n";
    /* The fragment colour multiplies the picture, channel by channel, so this
     * gives a scanline, aperture grille and vignette look with no read-back. The
     * black overlay of v0.80-46/47 peaked at 9.5% and was not noticed on hardware.
     * Lines come from the interpolated UV because gl_FragCoord sits at pixel
     * centres, and the grain is interleaved gradient noise, with no sin(). */
    static const char* fragment=
        "precision highp float;\n"
        "varying vec2 v_uv;\n"
        "uniform float u_time;\n"
        "uniform float u_strength;\n"
        "void main(){\n"
        " vec2 px=floor(v_uv*vec2(960.0,544.0));\n"
        " float scan=1.0-0.18*mod(px.y,2.0);\n"
        " float m=mod(px.x,3.0);\n"
        " vec3 mask=vec3(0.90)+0.10*vec3(step(m,0.5),step(0.5,m)*step(m,1.5),step(1.5,m));\n"
        " vec2 e=v_uv*(1.0-v_uv);\n"
        " float edge=1.0-clamp(e.x*e.y*17.0,0.0,1.0);\n"
        " float vignette=1.0-0.30*edge*edge;\n"
        " vec2 cell=floor(px/2.0)+vec2(floor(u_time*7.0)*5.0,0.0);\n"
        " float grain=fract(52.9829189*fract(dot(cell,vec2(0.06711056,0.00583715))));\n"
        " vec3 effect=mask*scan*vignette*(0.98+0.02*grain);\n"
        " gl_FragColor=vec4(mix(vec3(1.0),effect,u_strength),1.0);\n"
        "}\n";
    GLuint vs=vitaCrtModCompile(GL_VERTEX_SHADER,vertex);
    GLuint fs=vs?vitaCrtModCompile(GL_FRAGMENT_SHADER,fragment):0;
    if(!vs||!fs){
        if(vs)glDeleteShader(vs);
        if(fs)glDeleteShader(fs);
        vitaCrtModDisabled=1;
        log_line("VITAGL_CRT_MOD=compile_failed_safe_fallback");
        return false;
    }
    GLuint program=glCreateProgram();
    if(!program){
        glDeleteShader(vs);glDeleteShader(fs);vitaCrtModDisabled=1;return false;
    }
    glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocation(program,0,"a_position");
    glBindAttribLocation(program,1,"a_uv");
    log_line("VITAGL_CRT_MOD=overlay_link_begin");
    glLinkProgram(program);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!ok){
        char err[256]={0};glGetProgramInfoLog(program,255,NULL,err);
        log_line(err);glDeleteProgram(program);
        vitaCrtModDisabled=1;return false;
    }
    vitaCrtModProgram=program;
    log_line("VITAGL_CRT_MOD=overlay_ready_zero_readback");
    return true;
}
/* Only called under Experimental-GL, whose renderer is the legacy backend. */
static bool vita_crt_mod_render(Renderer* renderer,uint32_t frame,float strength){
    if(!vita_crt_mod_init())return false;
    /* Runner_drawGUI and every later overlay inherit this state, so all of it
     * goes back as found. The legacy renderer owns no VAO, and VitaGL cannot
     * report one, so the zero binding is simply kept. */
    GLint oldProgram=0,oldBuffer=0,oldFbo=0,oldViewport[4]={0,0,960,544};
    GLint oldEqRgb=GL_FUNC_ADD,oldEqAlpha=GL_FUNC_ADD;
    GLint oldAttribEnabled[2]={0,0};
    GLboolean oldMask[4]={GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE};
    GLboolean oldBlend=glIsEnabled(GL_BLEND);
    GLboolean oldDepth=glIsEnabled(GL_DEPTH_TEST);
    GLboolean oldScissor=glIsEnabled(GL_SCISSOR_TEST);
    GLboolean oldStencil=glIsEnabled(GL_STENCIL_TEST);
    GLboolean oldCull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&oldFbo);
    glGetIntegerv(GL_VIEWPORT,oldViewport);
    glGetIntegerv(GL_BLEND_EQUATION,&oldEqRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&oldEqAlpha);
    glGetBooleanv(GL_COLOR_WRITEMASK,oldMask);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled[0]);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled[1]);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,960,544);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ZERO,GL_SRC_COLOR,GL_ZERO,GL_ONE); /* destination x source */
#ifdef __vita__
    glBindVertexArray(0);
#endif
    glUseProgram(vitaCrtModProgram);
    GLint timeLoc=glGetUniformLocation(vitaCrtModProgram,"u_time");
    if(timeLoc>=0)glUniform1f(timeLoc,(float)(sceKernelGetProcessTimeWide()%100000000ULL)/1000000.0f);
    GLint strengthLoc=glGetUniformLocation(vitaCrtModProgram,"u_strength");
    if(strengthLoc>=0)glUniform1f(strengthLoc,strength);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),vitaCrtOverlayVertices);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),vitaCrtOverlayVertices+2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    while(glGetError()!=GL_NO_ERROR){}
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLenum drawError=glGetError();
    if(!oldAttribEnabled[1])glDisableVertexAttribArray(1);
    if(!oldAttribEnabled[0])glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
    glUseProgram((GLuint)oldProgram);
    glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)oldFbo);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glBlendEquationSeparate((GLenum)oldEqRgb,(GLenum)oldEqAlpha);
    /* VitaGL cannot report blend factors, but the renderer tracks them. */
    int32_t blendMode=renderer->vtable->gpuGetBlendMode(renderer);
    if(blendMode==bm_complex){
        GLLegacyRenderer* legacy=(GLLegacyRenderer*)renderer;
        renderer->vtable->gpuSetBlendModeExt(renderer,legacy->currentSFactor,
            legacy->currentDFactor,legacy->currentSFactorAlpha,legacy->currentDFactorAlpha);
    }else{
        renderer->vtable->gpuSetBlendMode(renderer,blendMode);
    }
    glBlendEquationSeparate((GLenum)oldEqRgb,(GLenum)oldEqAlpha);
    glColorMask(oldMask[0],oldMask[1],oldMask[2],oldMask[3]);
    if(!oldBlend)glDisable(GL_BLEND);
    if(oldDepth)glEnable(GL_DEPTH_TEST);
    if(oldScissor)glEnable(GL_SCISSOR_TEST);
    if(oldStencil)glEnable(GL_STENCIL_TEST);
    if(oldCull)glEnable(GL_CULL_FACE);
    static unsigned stableFrames=0;
    if(++stableFrames==90){
        sceIoRemove(VITA_CRT_MOD_PENDING);
        log_line("VITAGL_CRT_MOD=crash_guard_cleared_90_frames");
    }
    static int firstDrawLogged=0;
    if(!firstDrawLogged){
        firstDrawLogged=1;
        char line[80];
        snprintf(line,sizeof(line),"VITAGL_CRT_MOD=first_draw frame=%u gl_error=0x%04X",
                 (unsigned)frame,(unsigned)drawError);
        log_line(line);
    }
    return true;
}
