/* Low-cost CRT Mod: multiply the scene by a scanline, grille and vignette pattern.
 * No framebuffer capture, no extra render target/texture, no UV warp/zoom.
 * Runs before GML/native GUI; the scene beneath is never read back. */
static GLuint vitaCrtModProgram=0;
/* v0.83-6: optional clip for the overlay (CRT Agressivo uses it on the
 * console borders, outside the tube picture). x,y,w,h in GL window pixels. */
static int vitaCrtModClipOn=0;
static int vitaCrtModClip[4]={0,0,0,0};
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
    if(vitaCrtModClipOn){
        glEnable(GL_SCISSOR_TEST);
        glScissor(vitaCrtModClip[0],vitaCrtModClip[1],vitaCrtModClip[2],vitaCrtModClip[3]);
    }else glDisable(GL_SCISSOR_TEST);
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

/* v0.82 CRT Agressivo: a real tube. Instead of darkening the composed screen,
 * this REPLACES the final application_surface blit (GLCommon composite hook):
 * one full-screen pass samples the game surface through a barrel warp, so the
 * picture bulges and its corners round off, then applies scanlines that follow
 * the game's own 480 rows, an aperture grille, a vignette and a faint glass
 * glare. Same single draw as the blit it replaces: no extra render target, no
 * read-back. Borders are drawn before it and stay flat; the GUI and the port's
 * menus are drawn after it and stay undistorted. */
static GLuint vitaCrtTubeProgram=0;
static int vitaCrtTubeDisabled=0;
static GLint vitaCrtTubeTexLoc=-1,vitaCrtTubeRowsLoc=-1,vitaCrtTubeCurveLoc=-1;
static bool vita_crt_tube_init(void){
    if(vitaCrtTubeDisabled)return false;
    if(vitaCrtTubeProgram)return true;
    static const char* vertex=
        "attribute vec2 a_position; attribute vec2 a_uv;\n"
        "varying vec2 v_uv;\n"
        "void main(){v_uv=a_uv;gl_Position=vec4(a_position,0.0,1.0);}\n";
    static const char* fragment=
        "precision highp float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_tex;\n"
        "uniform float u_rows;\n"
        "uniform float u_curve;\n"
        "void main(){\n"
        " vec2 c=v_uv*2.0-1.0;\n"
        /* Barrel warp normalised so the middle of each edge stays put:
         * only the corners leave the picture and round off. */
        " c*=(1.0+u_curve*dot(c,c))/(1.0+u_curve);\n"
        " vec2 uv=c*0.5+0.5;\n"
        " vec2 inside=smoothstep(vec2(0.0),vec2(0.004),uv)*smoothstep(vec2(1.0),vec2(0.996),uv);\n"
        " vec3 col=texture2D(u_tex,clamp(uv,0.0,1.0)).rgb;\n"
        " float s=sin(uv.y*u_rows*3.14159265);\n"
        " float scan=mix(0.55,1.0,s*s);\n"
        " float m=mod(floor(gl_FragCoord.x),3.0);\n"
        " vec3 mask=vec3(0.88)+0.12*vec3(step(m,0.5),step(0.5,m)*step(m,1.5),step(1.5,m));\n"
        " vec2 e=uv*(1.0-uv);\n"
        " float vig=clamp(pow(max(e.x*e.y*16.0,0.0),0.22),0.0,1.0);\n"
        " float glare=0.035*(1.0-smoothstep(0.0,0.22,uv.y))*(1.0-abs(c.x));\n"
        " vec3 outc=col*1.24*scan*mask*vig+vec3(glare);\n"
        " gl_FragColor=vec4(outc*inside.x*inside.y,1.0);\n"
        "}\n";
    GLuint vs=vitaCrtModCompile(GL_VERTEX_SHADER,vertex);
    GLuint fs=vs?vitaCrtModCompile(GL_FRAGMENT_SHADER,fragment):0;
    if(!vs||!fs){
        if(vs)glDeleteShader(vs);
        if(fs)glDeleteShader(fs);
        vitaCrtTubeDisabled=1;
        log_line("VITAGL_CRT_TUBE=compile_failed_overlay_fallback");
        return false;
    }
    GLuint program=glCreateProgram();
    if(!program){glDeleteShader(vs);glDeleteShader(fs);vitaCrtTubeDisabled=1;return false;}
    glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocation(program,0,"a_position");
    glBindAttribLocation(program,1,"a_uv");
    glLinkProgram(program);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!ok){
        char err[256]={0};glGetProgramInfoLog(program,255,NULL,err);
        log_line(err);glDeleteProgram(program);
        vitaCrtTubeDisabled=1;
        log_line("VITAGL_CRT_TUBE=link_failed_overlay_fallback");
        return false;
    }
    vitaCrtTubeProgram=program;
    vitaCrtTubeTexLoc=glGetUniformLocation(program,"u_tex");
    vitaCrtTubeRowsLoc=glGetUniformLocation(program,"u_rows");
    vitaCrtTubeCurveLoc=glGetUniformLocation(program,"u_curve");
    log_line("VITAGL_CRT_TUBE=ready barrel_warp=1 scanlines=source_rows");
    return true;
}
static int vitaCrtTubeEnabledThisFrame=0;
static int vitaCrtTubeRect[4]={0,0,960,544}; /* sx,sy,ex,ey of the last tube draw */
static int vitaCrtTubeDrewThisFrame=0;
static bool vita_crt_tube_composite(GLuint texture,int32_t texW,int32_t texH,
                                    int32_t sx,int32_t sy,int32_t ex,int32_t ey,
                                    int32_t windowW,int32_t windowH){
    (void)texW;
    if(!vitaCrtTubeEnabledThisFrame||texture==0||windowW<=0||windowH<=0||
       !vita_crt_tube_init())
        return false;
    GLint oldProgram=0,oldBuffer=0,oldTexture=0,oldActive=GL_TEXTURE0;
    GLint oldAttribEnabled[2]={0,0};
    GLboolean oldBlend=glIsEnabled(GL_BLEND);
    GLboolean oldScissor=glIsEnabled(GL_SCISSOR_TEST);
    GLboolean oldDepth=glIsEnabled(GL_DEPTH_TEST);
    GLboolean oldStencil=glIsEnabled(GL_STENCIL_TEST);
    GLboolean oldCull=glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled[0]);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled[1]);
    glViewport(0,0,windowW,windowH);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
#ifdef __vita__
    glBindVertexArray(0);
#endif
    glUseProgram(vitaCrtTubeProgram);
    glBindTexture(GL_TEXTURE_2D,texture);
    /* The warp resamples: bilinear avoids crawling pixel seams. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    if(vitaCrtTubeTexLoc>=0)glUniform1i(vitaCrtTubeTexLoc,0);
    /* One scanline per native DELTARUNE row (the 640x480 surface is the
     * 320x240 art at 2x): a line per surface row beat against the 544-row
     * LCD and made moire bands in the PC preview. */
    if(vitaCrtTubeRowsLoc>=0)glUniform1f(vitaCrtTubeRowsLoc,(float)texH*0.5f);
    if(vitaCrtTubeCurveLoc>=0)glUniform1f(vitaCrtTubeCurveLoc,0.075f);
    /* Same rectangle as the blit. GL window y grows upward and the blit maps
     * the surface's row 0 to `ey`, so the top edge samples v=0. */
    const float x0=2.0f*(float)sx/(float)windowW-1.0f;
    const float x1=2.0f*(float)ex/(float)windowW-1.0f;
    const float yTop=2.0f*(float)ey/(float)windowH-1.0f;
    const float yBottom=2.0f*(float)sy/(float)windowH-1.0f;
    const GLfloat quad[]={
        x0,yBottom,0.0f,1.0f,
        x1,yBottom,1.0f,1.0f,
        x0,yTop,   0.0f,0.0f,
        x1,yTop,   1.0f,0.0f
    };
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),quad);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(GLfloat),quad+2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    while(glGetError()!=GL_NO_ERROR){}
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLenum drawError=glGetError();
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    if(!oldAttribEnabled[1])glDisableVertexAttribArray(1);
    if(!oldAttribEnabled[0])glDisableVertexAttribArray(0);
    glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture);
    glActiveTexture((GLenum)oldActive);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
    glUseProgram((GLuint)oldProgram);
    if(oldBlend)glEnable(GL_BLEND);
    if(oldScissor)glEnable(GL_SCISSOR_TEST);
    if(oldDepth)glEnable(GL_DEPTH_TEST);
    if(oldStencil)glEnable(GL_STENCIL_TEST);
    if(oldCull)glEnable(GL_CULL_FACE);
    if(drawError!=GL_NO_ERROR){
        vitaCrtTubeDisabled=1;
        char line[96];
        snprintf(line,sizeof(line),"VITAGL_CRT_TUBE=draw_error 0x%04X overlay_fallback",
                 (unsigned)drawError);
        log_line(line);
        return false;
    }
    vitaCrtTubeDrewThisFrame=1;
    vitaCrtTubeRect[0]=sx;vitaCrtTubeRect[1]=sy;vitaCrtTubeRect[2]=ex;vitaCrtTubeRect[3]=ey;
    static int firstLogged=0;
    if(!firstLogged){
        firstLogged=1;
        char line[128];
        snprintf(line,sizeof(line),"VITAGL_CRT_TUBE=first_draw rect=%d,%d-%d,%d rows=%d",
                 (int)sx,(int)sy,(int)ex,(int)ey,(int)texH);
        log_line(line);
    }
    return true;
}

/* v0.84-4 CRT Agressivo, full screen. The v0.82 tube warped only the game
 * picture in a heavy per-pixel shader (barrel maths, sin, pow, smoothstep for
 * every pixel): Chapter 5 logs measured ~15 ms per frame (cliff_verticalwind
 * 17 -> 32 ms, sethaqua_battle 14 -> 26 ms), and the console border stayed
 * flat. Now the border and the game are drawn into one 960x544 target
 * (GLCommon_endLetterboxBlit redirects both there) and a single pass curves
 * the whole screen:
 *  - the barrel warp lives in a 48x28 vertex grid (per vertex, not per pixel);
 *  - vignette, rounded tube edge and aperture grille are one precomputed
 *    960x544 mask texture;
 *  - scanlines follow the game's own rows through a 1x4 repeating texture.
 * The fragment shader is three texture reads and a multiply. */
#define VITA_CRT_W 960
#define VITA_CRT_H 544
#define VITA_CRT_GX 48
#define VITA_CRT_GY 28
#define VITA_CRT_CURVE 0.075f
#define VITA_CRT_GAIN 1.5f
static GLuint vitaCrtFullFbo=0,vitaCrtFullTex=0,vitaCrtMaskTex=0,vitaCrtScanTex=0,vitaCrtFullProgram=0;
static GLint vitaCrtFullTexLoc=-1,vitaCrtFullMaskLoc=-1,vitaCrtFullScanLoc=-1;
static int vitaCrtFullDisabled=0;
static GLfloat vitaCrtMesh[VITA_CRT_GX*VITA_CRT_GY*5];
static GLushort vitaCrtIndices[(VITA_CRT_GX-1)*(VITA_CRT_GY-1)*6];
static int vitaCrtMeshKey[5]={-1,-1,-1,-1,-1};

static void vita_crt_warp(float x,float y,float* u,float* v){
    /* x,y in NDC; returns the texture coordinate this screen point shows. */
    float k=(1.0f+VITA_CRT_CURVE*(x*x+y*y))/(1.0f+VITA_CRT_CURVE);
    *u=x*k*0.5f+0.5f; *v=y*k*0.5f+0.5f;
}

static bool vita_crt_full_init(void){
    if(vitaCrtFullDisabled)return false;
    if(vitaCrtFullProgram)return true;
    static const char* vertex=
        "attribute vec2 a_position; attribute vec2 a_uv; attribute float a_scan;\n"
        "varying vec2 v_uv; varying vec2 v_scr; varying float v_scan;\n"
        "void main(){v_uv=a_uv;v_scan=a_scan;v_scr=a_position*0.5+0.5;"
        "gl_Position=vec4(a_position,0.0,1.0);}\n";
    static const char* fragment=
        "precision mediump float;\n"
        "varying vec2 v_uv; varying vec2 v_scr; varying float v_scan;\n"
        "uniform sampler2D u_tex; uniform sampler2D u_mask; uniform sampler2D u_scan;\n"
        "void main(){\n"
        " vec3 col=texture2D(u_tex,v_uv).rgb;\n"
        " vec3 m=texture2D(u_mask,v_scr).rgb;\n"
        " float s=texture2D(u_scan,vec2(0.5,v_scan)).r;\n"
        " gl_FragColor=vec4(col*m*(s*1.5),1.0);\n"
        "}\n";
    GLuint vs=vitaCrtModCompile(GL_VERTEX_SHADER,vertex);
    GLuint fs=vs?vitaCrtModCompile(GL_FRAGMENT_SHADER,fragment):0;
    if(!vs||!fs){
        if(vs)glDeleteShader(vs);
        if(fs)glDeleteShader(fs);
        vitaCrtFullDisabled=1;
        log_line("VITAGL_CRT_FULL=compile_failed tube_fallback");
        return false;
    }
    GLuint program=glCreateProgram();
    glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocation(program,0,"a_position");
    glBindAttribLocation(program,1,"a_uv");
    glBindAttribLocation(program,2,"a_scan");
    glLinkProgram(program);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    glDeleteShader(vs);glDeleteShader(fs);
    if(!ok){glDeleteProgram(program);vitaCrtFullDisabled=1;
            log_line("VITAGL_CRT_FULL=link_failed tube_fallback");return false;}
    /* Render target for border + game. */
    glGenTextures(1,&vitaCrtFullTex);
    glBindTexture(GL_TEXTURE_2D,vitaCrtFullTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,VITA_CRT_W,VITA_CRT_H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&vitaCrtFullFbo);
    glBindFramebuffer(GL_FRAMEBUFFER,vitaCrtFullFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,vitaCrtFullTex,0);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    if(status!=GL_FRAMEBUFFER_COMPLETE){
        glDeleteFramebuffers(1,&vitaCrtFullFbo);glDeleteTextures(1,&vitaCrtFullTex);
        vitaCrtFullFbo=vitaCrtFullTex=0;glDeleteProgram(program);vitaCrtFullDisabled=1;
        log_line("VITAGL_CRT_FULL=fbo_incomplete tube_fallback");
        return false;
    }
    /* Mask: rounded tube edge x vignette x aperture grille x gain, in the
     * same maths as the v0.82 shader, evaluated once per screen pixel. */
    unsigned char* mask=(unsigned char*)malloc((size_t)VITA_CRT_W*VITA_CRT_H*4);
    if(mask==NULL){vitaCrtFullDisabled=1;return false;}
    for(int y=0;y<VITA_CRT_H;++y){
        for(int x=0;x<VITA_CRT_W;++x){
            float nx=((float)x+0.5f)/VITA_CRT_W*2.0f-1.0f;
            float ny=((float)y+0.5f)/VITA_CRT_H*2.0f-1.0f;
            float u,v;vita_crt_warp(nx,ny,&u,&v);
            float iu=u<=0.0f||u>=1.0f?0.0f:(u<0.004f?u/0.004f:(u>0.996f?(1.0f-u)/0.004f:1.0f));
            float iv=v<=0.0f||v>=1.0f?0.0f:(v<0.004f?v/0.004f:(v>0.996f?(1.0f-v)/0.004f:1.0f));
            float e=u*(1.0f-u)*v*(1.0f-v)*16.0f;
            float vig=e<=0.0f?0.0f:powf(e,0.22f);
            if(vig>1.0f)vig=1.0f;
            float base=iu*iv*vig*1.24f/VITA_CRT_GAIN;
            int m=x%3;
            float g[3]={0.88f,0.88f,0.88f};g[m]=1.0f;
            unsigned char* px=mask+((size_t)y*VITA_CRT_W+x)*4;
            for(int c=0;c<3;++c){
                float val=base*g[c]*255.0f+0.5f;
                px[c]=(unsigned char)(val>255.0f?255.0f:val);
            }
            px[3]=255;
        }
    }
    glGenTextures(1,&vitaCrtMaskTex);
    glBindTexture(GL_TEXTURE_2D,vitaCrtMaskTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,VITA_CRT_W,VITA_CRT_H,0,GL_RGBA,GL_UNSIGNED_BYTE,mask);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    free(mask);
    /* Scanline profile mix(0.55,1,sin^2) sampled at 4 points, repeated. */
    static const unsigned char scan[16]={140,140,140,255, 198,198,198,255,
                                         255,255,255,255, 198,198,198,255};
    glGenTextures(1,&vitaCrtScanTex);
    glBindTexture(GL_TEXTURE_2D,vitaCrtScanTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,4,0,GL_RGBA,GL_UNSIGNED_BYTE,scan);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D,0);
    int n=0;
    for(int j=0;j<VITA_CRT_GY-1;++j)for(int i=0;i<VITA_CRT_GX-1;++i){
        GLushort a=(GLushort)(j*VITA_CRT_GX+i),b=(GLushort)(a+1),
                 c=(GLushort)(a+VITA_CRT_GX),d=(GLushort)(c+1);
        vitaCrtIndices[n++]=a;vitaCrtIndices[n++]=b;vitaCrtIndices[n++]=c;
        vitaCrtIndices[n++]=b;vitaCrtIndices[n++]=d;vitaCrtIndices[n++]=c;
    }
    vitaCrtFullProgram=program;
    vitaCrtFullTexLoc=glGetUniformLocation(program,"u_tex");
    vitaCrtFullMaskLoc=glGetUniformLocation(program,"u_mask");
    vitaCrtFullScanLoc=glGetUniformLocation(program,"u_scan");
    log_line("VITAGL_CRT_FULL=ready mesh=48x28 mask=960x544 borders_warped=1");
    return true;
}

/* Mesh: position, warped texture coordinate, scanline phase (game rows). */
static void vita_crt_full_mesh(int32_t texH,int32_t sy,int32_t ey){
    int key[5]={texH,sy,ey,VITA_CRT_W,VITA_CRT_H};
    if(memcmp(key,vitaCrtMeshKey,sizeof(key))==0)return;
    memcpy(vitaCrtMeshKey,key,sizeof(key));
    float rows=(float)texH*0.5f;
    float span=(float)(ey-sy);if(span<1.0f)span=1.0f;
    for(int j=0;j<VITA_CRT_GY;++j)for(int i=0;i<VITA_CRT_GX;++i){
        float x=(float)i/(VITA_CRT_GX-1)*2.0f-1.0f;
        float y=(float)j/(VITA_CRT_GY-1)*2.0f-1.0f;
        float u,v;vita_crt_warp(x,y,&u,&v);
        GLfloat* m=vitaCrtMesh+(j*VITA_CRT_GX+i)*5;
        m[0]=x;m[1]=y;m[2]=u;m[3]=v;
        /* GL rows grow upward; game row 0 is at ey. */
        m[4]=((float)ey-v*VITA_CRT_H)/span*rows;
    }
}

GLuint vita_crt_full_target(void){
    if(!vitaCrtTubeEnabledThisFrame||!vita_crt_full_init())return 0;
    return vitaCrtFullFbo;
}

/* GLCommon fullscreen hook: border + game are in vitaCrtFullTex; draw the
 * curved screen onto the window. */
static bool vita_crt_full_present(int32_t texH,int32_t sx,int32_t sy,int32_t ex,int32_t ey,
                                  int32_t windowW,int32_t windowH){
    (void)sx;(void)ex;
    GLint oldProgram=0,oldBuffer=0,oldTexture=0,oldActive=GL_TEXTURE0;
    GLint oldAttribEnabled[3]={0,0,0};
    GLboolean oldBlend=glIsEnabled(GL_BLEND);
    GLboolean oldScissor=glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM,&oldProgram);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&oldBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&oldActive);
    for(int a=0;a<3;++a)glGetVertexAttribiv(a,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&oldAttribEnabled[a]);
    vita_crt_full_mesh(texH,sy,ey);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,windowW,windowH);
    glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
#ifdef __vita__
    glBindVertexArray(0);
#endif
    glUseProgram(vitaCrtFullProgram);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,vitaCrtScanTex);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,vitaCrtMaskTex);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);
    glBindTexture(GL_TEXTURE_2D,vitaCrtFullTex);
    if(vitaCrtFullTexLoc>=0)glUniform1i(vitaCrtFullTexLoc,0);
    if(vitaCrtFullMaskLoc>=0)glUniform1i(vitaCrtFullMaskLoc,1);
    if(vitaCrtFullScanLoc>=0)glUniform1i(vitaCrtFullScanLoc,2);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,5*sizeof(GLfloat),vitaCrtMesh);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(GLfloat),vitaCrtMesh+2);
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,5*sizeof(GLfloat),vitaCrtMesh+4);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    while(glGetError()!=GL_NO_ERROR){}
    glDrawElements(GL_TRIANGLES,(GLsizei)(sizeof(vitaCrtIndices)/sizeof(vitaCrtIndices[0])),
                   GL_UNSIGNED_SHORT,vitaCrtIndices);
    GLenum drawError=glGetError();
    for(int a=0;a<3;++a)if(!oldAttribEnabled[a])glDisableVertexAttribArray(a);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,(GLuint)oldTexture);
    glActiveTexture((GLenum)oldActive);
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)oldBuffer);
    glUseProgram((GLuint)oldProgram);
    if(oldBlend)glEnable(GL_BLEND);
    if(oldScissor)glEnable(GL_SCISSOR_TEST);
    /* The whole screen, border included, went through the tube: the
     * border-strip overlay in playable_main has nothing left to do. */
    vitaCrtTubeDrewThisFrame=1;
    vitaCrtTubeRect[0]=0;vitaCrtTubeRect[1]=0;vitaCrtTubeRect[2]=windowW;vitaCrtTubeRect[3]=windowH;
    static int firstLogged=0;
    if(!firstLogged){
        firstLogged=1;
        char line[128];
        snprintf(line,sizeof(line),"VITAGL_CRT_FULL=first_draw game_rect=%d,%d-%d,%d rows=%d gl_error=0x%04X",
                 (int)sx,(int)sy,(int)ex,(int)ey,(int)texH,(unsigned)drawError);
        log_line(line);
    }
    if(drawError!=GL_NO_ERROR){
        vitaCrtFullDisabled=1;   /* next frames use the v0.82 tube */
        log_line("VITAGL_CRT_FULL=draw_error tube_fallback");
    }
    return true;
}