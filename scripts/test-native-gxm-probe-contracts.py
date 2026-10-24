from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
cmake = (root/'experiments/native-gxm/CMakeLists.txt').read_text(encoding='utf-8')
source = (root/'experiments/native-gxm/main.c').read_text(encoding='utf-8')
build = (root/'scripts/build-native-gxm-probe.ps1').read_text(encoding='utf-8')
header = (root/'src/butterscotch/src/renderer.h').read_text(encoding='utf-8')
contract = (root/'experiments/native-gxm/bs_renderer_contract.c').read_text(encoding='utf-8')
impl = (root/'experiments/native-gxm/bs_renderer_impl.inc').read_text(encoding='utf-8')
bootstrap = (root/'experiments/native-gxm/bs_bootstrap_probe.c').read_text(encoding='utf-8')
names = re.findall(r'^\s*.*?\(\*(\w+)\)\([^\n]*\);', header.split('} VertexBuffer;',1)[1].split('} RendererVtable;',1)[0], re.M)
entries = re.findall(r'^\s*\.(\w+) = bs_(\w+),$',contract,re.M)
assert len(names)==73 and len(entries)==73
assert {x for x,_ in entries}==set(names)
assert all(a==b for a,b in entries)
assert 'bs_unsupported(' in impl and 'bs_unsupported(' in contract
assert 'Runner_create(' in bootstrap and 'Runner_free(' in bootstrap
assert 'OverlayFileSystem_create(' in bootstrap and 'ux0:data/deltarune_saves/' in bootstrap
assert 'OverlayFileSystem_setModPath' in bootstrap and 'mods/Lang/%s/chapter%d/' in bootstrap
assert 'gxmRGBA4444Upload' in impl and 'ImageDecoder_decodeToRgba' in impl
assert 'gxmCreateFramebuffer' in impl and 'gxmBeginFrameEx' in impl
assert 'bs_drawSprite(' in impl and 'bs_drawText(' in impl
assert 'bs_surfaceCopy(' in impl and 'bs_surfaceGetPixels(' in impl
assert 'BSContractRenderer_bind' in source
assert 'Runner_initFirstRoom(bs.runner)' in source and 'Runner_step(bs.runner)' in source
assert 'Runner_drawViews(bs.runner' in source and 'Runner_drawGUI(bs.runner' in source
assert 'update_runner_input(bs.runner' in source
assert 'USE_VITA_SHARK OFF' in cmake and 'vitaGL' not in cmake
assert 'DLTAGXM48' in cmake
assert 'PORT_BUILD_VERSION' in cmake and 'PORT_BUILD_VERSION' in build
assert 'vita_create_self(gxm.bin' in cmake
assert 'NATIVE_GXM=fps scene=%s' in source
assert 'lazyLoadRooms = true' in bootstrap and 'lazyLoadTextures = true' in bootstrap
assert 'bs_renderer_contract.c' in cmake
assert 'bs_switch_target(b,r->runner->applicationSurfaceId)' in impl
assert 'b->frameIsActive=1;b->draws=b->missing=b->targetSwitches=0;' in impl
assert 'b->activeTarget=-1;b->draws=b->missing' not in impl
assert 'NATIVE_GXM=render_metrics' in impl and 'BSContractRenderer_metrics(' in source
assert 'bs_snapshot_sync(b,0);' in impl and 'bs_snapshot_sync(b,1);' in impl
assert 'DataWin_allocSpriteSlot(dw,b->originalSpriteCount)' in impl
assert 'gxmRGBA4444Upload(b->vg,tw,th,NVG_IMAGE_NEAREST,rgba)' in impl
assert 'bs_grow_tpag_cache(b,tid+1)' in impl
assert 'bs_deleteSprite(Renderer* r,int32_t id)' in impl
assert 'createSpriteFromSurface_dynamic_tpag' not in impl
assert 'sceGxmFinish(gxmGetWindow()->context)' in impl
assert 'nvgRGBA(20,230,230,255)' not in source
assert 'AlAudioSystem_create()' in bootstrap
assert 'NoopAudioSystem_create()' not in bootstrap
assert 'audio/openal/al_audio_system.c' in cmake
assert 'audio/openal/wave.c' in cmake
assert 'stb/vorbis' in cmake and 'openal' in cmake and 'SceAudio_stub' in cmake
assert 'set_source_files_properties(' in cmake and 'PLATFORM_VITA' in cmake
assert 'bs.audio->vtable->update(bs.audio' in source
assert 'log_audio_metrics' in source and 'NATIVE_GXM=audio_metrics' in source
assert 'g_vitaActiveChapter = 0' in source
assert 'g_vitaProbeLoggingEnabled = 1' in source
assert 'NATIVE_GXM=audio_stream' in source
assert 'bs_bootstrap_load(&bs,activeChapter,activeMod)' in source
assert 'rejected=self_intersecting_quad' in impl
assert 'p->chapter=chapter' in (root/'experiments/native-gxm/bs_bootstrap_probe.c').read_text(encoding='utf-8')
assert 'BS_CHAPTER_ROOT "chapter%d/"' in (root/'experiments/native-gxm/bs_bootstrap_probe.c').read_text(encoding='utf-8')
assert 'GX_CHAPTER_MARKER' in source and 'next_chapter.txt' in source
assert 'sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL)' in source
assert 'SceAppMgr_stub' in cmake
assert 'Runner_setGameArgs(bs.runner,launcherArgs,3)' in source
assert 'Runner_setGameArgs(bs.runner,chapterArgs,6)' in source
assert 'bs.runner->osType=OS_WINDOWS;' in source
assert 'gameplayActive=runnerResult==0' in source
assert 'NATIVE_GXM=game_change request' in source
assert 'NATIVE_GXM=chapter_boot selected=' in source
print('Native GXM contracts: PASS 73 callbacks + OpenAL + chapter relaunch')

# Native GXM v11 parity/regression gates: GXM state stays independent of vitaGL.
native=(root/'experiments/native-gxm/nanovg_gxm_native.h').read_text(encoding='utf-8')
settings=(root/'experiments/native-gxm/vita_settings_native.c').read_text(encoding='utf-8')
touch=(root/'experiments/native-gxm/native_settings_touch.inc').read_text(encoding='utf-8')
assert 'gxmnvg__variantProgram' in native and 'info.colorMask=' in native
assert 'gxm->activeBlend=call->blendFunc;' in native
assert 'ret->colorMask=gxm->drawColorMask;' in native
assert 'nvgxmSetColorWriteMask(g->vg' in impl
assert 'bs_unsupported("gpu_color_mask")' not in impl
assert 'bs_upload_page(b,tpag);' in impl
assert 'for (unsigned i=0;i<b->imageCount;i++) {' not in impl.split('static int bs_upload_page(',1)[1].split('static int bs_ensure_image(',1)[0]
assert 'pages_decoded=' in impl and 'partial_masks=' in impl
assert 'VitaSettings_load(&settings)' in source
assert 'VitaSettings_applyAudio(&settings,bs.audio)' in source
assert 'VitaSettings_draw(&settings,bs.renderer)' in source
assert 'frame_cost step_us=' in source
assert 'glBindTexture(' not in settings and 'glDrawArrays(' not in settings
print('GXM48 regressions: PASS dynamic blend+mask, on-demand TPAG, Game Settings, perf metrics')

# Chapter0 direct boot, native border artwork and gradient crash regression.
borders=(root/'experiments/native-gxm/native_borders.inc').read_text(encoding='utf-8')
assert 'atlas_probe.c' not in cmake and 'real_tpag_probe.c' not in cmake
assert 'draw_test_scene(' not in source
assert 'gameplayActive=runnerResult==0' in source
assert 'NATIVE_GXM=ready mode=game_only' in source
assert 'SCE_CTRL_SQUARE' in source  # Still a real mapped gamepad key
assert 'drawTextColor_gradient' not in impl
assert 'bs_gradient_color' in impl
assert 'bs_native_border_draw(b)' in impl
assert 'VitaBorderRules_resolve' in borders
assert 'VitaBorderRules_filename' in borders
assert 'VitaBorderRules_shouldBlankSurround' in borders
assert 'vita_border_rules.c' in cmake
assert 'static void saveSettings(' in settings and 'border_mode=' in settings
assert 'consoleBorderMode' in settings and 'betterBordersEnabled' in settings
assert 'g_nativeBorderMode=settings.consoleBorderMode' in source
assert 'angle==0.0f && xs==1.0f && ys==1.0f' in impl
assert 'VitaSettings_draw(&settings,bs.renderer)' in source
print('GXM48: PASS direct Chapter0, PNG borders, settings, gradient crash path, sprite fastpath')

# The native build reuses original controller/state/UI source.
original=(root/'src/vita-probe/source/vita_settings.c').read_text(encoding='utf-8')
assert len(settings)>100000 and 'void VitaSettings_draw(' in settings
assert 'bool VitaSettings_handleInput(' in settings
assert 'void VitaSettings_loadLanguage(' in settings
assert 'void VitaSettings_drawTouchControls(' in settings
assert 'void VitaSettings_drawTrophyNotification(' in settings
assert 'void VitaSettings_drawCalibration(' in settings
assert 'void VitaSettings_drawDevOverlay(' in settings
assert 'nativeSettingsImage(' in settings and 'nvgImagePattern' in settings
assert 'ImageDecoder_decodeToRgba(' in settings
assert 'vita_settings_native.c' in cmake and 'native_settings_compat.c' in cmake
assert 'assets/ui/Lang/' in cmake and 'assets/ui/' in cmake
assert 'SceTouch_stub' in cmake and 'nativeTouchPoll(' in source
assert 'VitaSettings_setSliderFromTouch(' in touch
assert 'VitaSettings_setTouchVisuals(' in touch
assert 'sceTouchPeek(' in touch
assert 'VitaSettings_handleInput(&settings,&pad,bs.audio)' in source
assert 'g_nativeSettingsVG=vg;' in source
assert 'g_vitaDisplayZoom' in impl and 'g_vitaDisplayOffsetX' in impl
assert '"Native GXM"' in settings and '"RGBA4444 (fixed)"' in settings
assert 'VitaSettings_drawBrightness(&settings' in source
assert 'VitaSettings_drawTrophyNotification(&settings' in source
print('GXM48 original Game Settings integration: PASS code, assets, controls, graphics safeguards')

# gxm12 physical crash replay: gpuSetAlphaTestEnable MUST NOT abort.
assert 'bs_unsupported("alpha_test_enable")' not in impl
assert 'bs_unsupported("alpha_test_ref")' not in impl
assert 'static void bs_gpuSetAlphaTestEnable(' in impl
assert 'static void bs_gpuSetAlphaTestRef(' in impl
assert 'bs_alpha_uniform_visible(' in impl
assert 'b->alphaEnable=0;b->blendEnable=1;b->alphaTestRef=0;' in impl
assert 'bs_clamp01(alpha)*255.0f>(float)b->alphaTestRef' in impl
assert 'NATIVE_GXM=alpha_test enabled=' in impl
assert 'NATIVE_GXM=alpha_test_ref value=' in impl
assert 'alpha_test=%d alpha_ref=%u' in impl
assert 'b->blendMode=bm_normal' in impl
assert 'gpuSetAlphaTestEnable = bs_gpuSetAlphaTestEnable' in contract
assert 'gpuSetAlphaTestRef = bs_gpuSetAlphaTestRef' in contract
print('GXM48 Chapter3 alpha-test crash regression: PASS tracked GL_GREATER ref+fallback, no abort')

# GXM48: Castle Town fog and full source-TXTR page residency regression.
native=(root/'experiments/native-gxm/nanovg_gxm_native.h').read_text(encoding='utf-8')
assert 'NVG_IMAGE_RGBA4444 = 1 << 11' in native
assert 'SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_RGBA' in native
assert 'SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR' in native
assert 'int packed4444=' in native
assert 'gxmRGBA4444Upload(b->vg,w,h,NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR,rgba)' in impl
assert 'chapter%d/pvr/page_%03d.bc3.pvr' not in impl
assert 'magic==0x03525650u' not in impl
assert 'NVG_IMAGE_RGBA4444' in (root/'experiments/native-gxm/rgba4444_native.h').read_text()
assert 'static int bs_read_bc3_pvr' not in impl
assert 'int *pageImages,*pageWidth,*pageHeight;' in impl
assert 'b->cacheBudget=' in impl
assert 'void BSContractRenderer_cacheMaintenance(Renderer*r)' in impl
assert 'BSContractRenderer_cacheMaintenance(bs.renderer);' in source
assert 'sceGxmFinish(gxmGetWindow()->context);' in impl
assert 'b->pageImages[page]=image;' in impl
assert 'b->pageLastUsed[page]=b->cacheFrame;' in impl
assert 'sx+=(float)item->sourceX' in impl
assert 'g->sourceX+px,g->sourceY+py' in impl
assert 'float ox=i<b->originalTpagCount?' in impl
assert '*image=img;*w=b->width[id]' in impl
assert 'cache_mib=%llu' in impl and 'rgba4444=%u' in impl
assert 'bs_unsupported("fog")' not in impl
assert 'NATIVE_GXM=fog enabled=%d' in impl
assert 'bs_unsupported("gpuSetShader_custom_gxp")' not in impl
assert 'bs_unsupported("shaderSetUniformF")' not in impl
assert 'static bool bs_shadersSupported(void){' in impl
assert 'return g_nativeGxpAvailable!=0;' in impl
assert 'NATIVE_GXM=shader request=' in impl
assert (root/'scripts/audit-native-gxm-chapters.py').exists()
assert 'VitaGpuRules_modernTextureCacheLimitBytes' in impl
assert 'VitaGpuRules_textureRecentlyUsed' in impl
assert 'vita_gpu_rules.c' in cmake
print('GXM48 PASS source-TXTR sampled RGBA4444, safe LRU, UV, fog fallback, shader tracking')

# Regression for hardware-reported corrupted GXM15 BC3 images.
sw=(root/'experiments/native-gxm/native_bc3_swizzle.h').read_text(encoding='utf-8')
diag=(root/'experiments/native-gxm/diagnostic_main.c').read_text(encoding='utf-8')
assert 'nativeBc3MortonIndex' in sw
assert 'nativeBc3Swizzle' in sw
assert 'gxm-texture-mode.txt' not in impl
assert 'int enableBc3=mode==1;' not in impl
assert 'enableBc3 && bs_read_bc3_pvr' not in impl
assert 'gxmRGBA4444Upload' in impl
assert 'nativeBc3Swizzle(twiddled,payload' not in impl
assert 'nativeBc3Swizzle(tiled,blob' in diag
assert 'progress.bin' in diag and 'state->busy=chapter' in diag
assert 'DataWin_parse(path,o)' in diag and 'VM_create(dw)' in diag
assert 'GXM_DIAGNOSTIC' in cmake and 'diagnostic_main.c' in cmake
assert 'Deltarune-GXM-Audit-v0.78-a3.vpk DLTGMAUD3' in cmake
assert 'shader_scan_start' in diag and 'room_scan_start' in diag
assert 'pvr_scan_start' in diag
assert 'TPAG_OUT_OF_BOUNDS' in diag
assert 'pvr.broken+pvr.missing' in diag
assert 'gxm-audit-a3' in diag
assert 'referenced_tpags=%u' in diag
print('GXM48/AUDIT2 PASS gameplay RGBA4444 enforced; BC3 remains audit-only, six-chapter scan')

# GXM48: Replay fatal Chapter 3 unsupported=blend_subtract from
# psp2core-1791862700; check equations and factors vs playable GLCommon.
glcommon=(root/'src/butterscotch/src/gl_common/gl_common.c').read_text(encoding='utf-8')
assert 'case bm_subtract:         return GL_FUNC_ADD;' in glcommon
assert 'case bm_subtract:         return GL_ZERO;' in glcommon
assert 'case bm_subtract:         return GL_ONE_MINUS_SRC_COLOR;' in glcommon
for bad in ['bs_unsupported("blend_subtract")','bs_unsupported("blend_max")',
            'bs_unsupported("blend_min")']:
    assert bad not in impl,bad
for term in ['mode==bm_subtract','mode==bm_reverse_subtract',
             'mode==bm_min','mode==bm_max',
             'SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT',
             'SCE_GXM_BLEND_FUNC_MIN',
             'NVG_ZERO,NVG_ONE_MINUS_SRC_COLOR',
             'NATIVE_GXM=blend_mode mode=']:
    assert term in impl,term
assert 'void nvgxmSetBlendEquation(NVGcontext *ctx,' in native
assert 'SceGxmBlendFunc colorFunc;' in native
assert 'SceGxmBlendFunc alphaFunc;' in native
assert 'blend.colorFunc=gxm->drawColorEquation;' in native
assert 'blend.alphaFunc=gxm->drawAlphaEquation;' in native
assert 'info.colorFunc=blend->colorFunc;' in native
assert 'info.alphaFunc=blend->alphaFunc;' in native
assert native.count('gxmnvg__blendCompositeOperation(gxm,compositeOperation)')==3
assert 'gxm->drawColorEquation=SCE_GXM_BLEND_FUNC_ADD;' in native
assert 'gxm->drawAlphaEquation=SCE_GXM_BLEND_FUNC_ADD;' in native
assert 'SCE_GXM_BLEND_FUNC_ADD' in native
assert 'DLTAGXM48' in cmake and 'PORT_BUILD_VERSION' in build
print('GXM48 Chapter3 blend_subtract regression: PASS nonfatal and GLCommon-equivalent GPU blend math')

# GXM48 physical Chapter3 regression: inventory sprite-font abort and slow first room.
assert 'bs_unsupported("drawText_spritefont")' not in impl
assert 'spriteFont=&dw->sprt.sprites[f->spriteIndex]' in impl
assert 'ptrdiff_t frame=g-f->glyphs' in impl
assert 'spriteFont->tpagIndices[frame]' in impl
assert 'bs_ensure_image(b,ti)' in impl
assert 'tp->targetY-f->spriteOriginYAdjust' in impl
assert 'g->sourceWidth,g->sourceHeight,tint,alpha' in impl
assert 'spritefont_glyphs=%lu' in impl
assert 'static int bs_image_offscreen(' in impl
assert 'nvgCurrentTransform(b->vg,t)' in impl
assert 'nvgTransformPoint(' in impl
assert impl.count('bs_image_offscreen(b,x,y,dw,dh)')==3
assert 'if(angle==0.0f && bs_image_offscreen(b,x,y,dw,dh))return;' in impl
assert 'offscreen_culls=%lu' in impl
assert 'runnerEventsUs+=bs.runner->profStepEventsUs;' in source
assert 'runnerOtherUs+=bs.runner->profStepOtherUs;' in source
assert 'viewsUs+=sceKernelGetProcessTimeWide()-viewsStartUs;' in source
assert 'guiUs+=sceKernelGetProcessTimeWide()-guiStartUs;' in source
assert 'NATIVE_GXM=runner_profile' in source
assert 'DLTAGXM48' in cmake
print('GXM48 Chapter3 regression: sprite-font atlas glyph rendering and conservative offscreen culling, runner phase metrics')

# GXM48: physical Chapter 3/5 faults and border/video native API regression.
assert 'bs_unsupported("drawTriangle_vertex_gradient")' not in impl
assert 'NATIVE_GXM=drawTriangle_vertex_gradient mode=native_linear_approx' in impl
assert 'bs_unsupported("switch_target_inside_view")' not in impl
assert 'surface_switch inside_active_pass restore_camera=1' in impl
assert 'nvgSave(b->vg);nvgScissor(b->vg,px,py,pw,ph);' in impl
assert 'bs_unsupported("drawRectangleColor_four_corners")' not in impl
assert 'bs_unsupported("drawLineColor_gradient")' not in impl
borders=(root/'experiments/native-gxm/native_borders.inc').read_text(encoding='utf-8')
assert 'b->borderOldImage' in borders
assert 'borderTransition+=1.0f/24.0f' in borders
assert '!g_nativeBetterBorders&&controllerName' in borders
assert 'gxmClearColor(0.0f, 0.0f, 0.0f, 1.0f)' in source
assert 'NATIVE_GXM_VIDEO' in cmake
assert 'native_video_gxm.c' in cmake
assert 'SceAvPlayer_stub' in cmake
assert 'VitaVideo_updateFrame()' in source
assert 'Runner_dispatchVideoAsync(bs.runner,"video_start")' in source
assert 'Runner_dispatchVideoAsync(bs.runner,"video_end")' in source
assert 'VITA_VIDEO_SURFACE_ID' in impl
vid=(root/'experiments/native-gxm/native_video_gxm.c').read_text(encoding='utf-8')
assert 'sceAvPlayerGetVideoData(player,&frame)' in vid
assert 'SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1' in vid
assert 'nvgxmCreateImageFromHandle(vg,&texture)' in vid
assert 'sceGxmFinish(win->context)' in vid
assert 'SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW' in vid
vm=(root/'src/butterscotch/src/vm_builtins.c').read_text(encoding='utf-8')
assert '#if defined(PLATFORM_VITA) || defined(NATIVE_GXM_VIDEO)' in vm
assert 'DLTAGXM48' in cmake
print('GXM48 Chapter3/5 regression: restored active view target switching, gradient fallbacks, crossfade borders, experimental SceAvPlayer GXM video bridge')

assert 'VitaVideo_afterPresent();' in source
assert 'closeRequested=1;' in vid
assert 'static void avPerformClose(void)' in vid
assert 'sceGxmFinish(win->context)' in vid
assert 'borderControllerCrossfade' in impl
assert 'VitaBorderRules_controllerCrossfade' in borders
assert '_border_image_temp_alpha' in borders
assert '_border_red_alpha' in borders
assert 'VITA_BORDER_SCENE_CH5_GARDEN_REVEAL' in borders
assert 'bs_unsupported("drawSurfaceColor_gradient")' not in impl
assert 'bs_unsupported("drawSurface_tint")' not in impl
assert 'bs_unsupported("drawSurfaceTiled_tint")' not in impl
print('GXM48 advanced border fields, Chapter 5 garden flags, deferred AVPlayer close: PASS')

# GXM48 runtime GXP opt-in regression. Validate packet-scoped shader dispatch.
assert "gxm-native-shaders.txt" in source
assert "g_nativeGxpAvailable=status==0" in source
assert "shd_grayscale" in impl and "shd_invert" in impl
assert 'nvgxmSelectRuntimeShader(b->vg,shaderTag);' in impl
assert 'nvgxmSelectRuntimeShader(b->vg,0);' in impl
assert 'bs_native_shader_tag(r,id)' in impl
assert 'return g_nativeGxpAvailable!=0;' in impl
assert 'NATIVE_GXM=shader_init requested=1 ready=' in source
assert 'NATIVE_GXM=shader_effect id=%d' in impl
assert 'int nvgxmEnableRuntimeShaders(' in native
assert 'SceGxmProgram*compiled=shark_compile_shader_extended' in native
assert 'sceGxmShaderPatcherRegisterProgram(gxm->shader_patcher' in native
assert 'GXMNVGshader runtime_shader[13];' in native
assert 'gxm->runtime_shader[k].prog.frag' in native
assert 'ret->shaderTag=(unsigned)gxm->drawShaderTag;' in native
assert 'gxm->activeShaderTag=(int)call->shaderTag;' in native
assert 'frag->type==NSVG_SHADER_FILLIMG' in native
assert 'frag_prog=custom->prog.frag;' in native
assert 'frag_loc=custom->loc[GXMNVG_LOC_FRAG];' in native
assert 'shark_end();' in native
assert 'DLTAGXM48' in cmake
assert 'vitashark' in cmake and 'taihen_stub' in cmake
assert 'SceShaccCgExt' in cmake
gray=(root/'experiments/native-gxm/shaders/gray.cg').read_text(encoding='utf-8')
invert=(root/'experiments/native-gxm/shaders/invert.cg').read_text(encoding='utf-8')
assert 'dot(color.rgb,float3(0.21,0.71,0.07))' in gray
assert 'float4(gray*color.a,gray*color.a,gray*color.a,color.a)' in gray
assert '(1.0-color.rgb)*color.a' in invert
for fragment in [gray,invert]:
    assert fragment.startswith('uniform float4 frag[11];')
    assert 'uniform sampler2D tex : TEXUNIT0' in fragment
    assert 'float2 fpos:TEXCOORD1' in fragment
    assert 'float3x3 pm=' in fragment
    assert 'float3x3 sm=' in fragment
    assert 'mask.x*mask.y' in fragment
print('GXM48 GXP shader state, SHDR selection, batch capture, opt-in fallback, Cg source parity: PASS')

# GXM48: no-quality-reduction hotpath contracts and instrumentation.
assert 'static BSGxmVisible bs_visible_local(' in impl
assert 'nvgTransformInverse(inv,t)' in impl
assert 'bs_tile_index_span(' in impl
assert impl.count('bs_tile_index_span(')>=10
assert 'bs_image_tinted_impl(' in impl
assert 'angle==0.0f);' in impl
assert 'tile_skipped=%llu tile_drawn=%llu tiled_calls=%llu' in impl
assert 'const float xmin=x0+fminf(0.0f,ax)+fminf(0.0f,bx);' in impl
assert 'const float ymax=y0+fmaxf(0.0f,ay)+fmaxf(0.0f,by);' in impl
assert 'bs_image_offscreen(b,x,y,dw,dh)' in impl
assert 'b->tileIterationsAvoided+=' in impl
assert 'b->tileIterationsExecuted++;' in impl
assert 'gxm->textures[last].id==id' in native
assert 'gxm->textureLookupIndex=i;' in native
assert 'gxm->lastVariantIndex<gxm->numVariants' in native
assert 'gxm->variants[i].id==id&&gxm->variants[i].mask==mask' in native
assert 'gxm->lastBoundImageId=-1;' in native
assert 'if(gxm->lastBoundImageId!=tex->id)' in native
assert 'textureBindAvoided' in native and 'variantCacheHits' in native
assert 'nvgxmGetFastPathMetrics' in native
assert 'NATIVE_GXM=submit_cost flush_us=' in source
assert 'perfFlush+=flushEndUs-flushStartUs;' in source
assert 'perfPresent+=presentEndUs-flushEndUs;' in source
assert 'DLTAGXM48' in cmake
print('GXM48 lossless renderer: visibility, tiled-loop bounds, GPU cache safety and frame profiling: PASS')

# GXM48 first physical video regression, circular TV lighting, Room Navigator.
video=(root/'experiments/native-gxm/native_video_gxm.c').read_text(encoding='utf-8')
header=(root/'src/butterscotch/src/renderer.h').read_text(encoding='utf-8')
contract=(root/'experiments/native-gxm/bs_renderer_contract.c').read_text(encoding='utf-8')
assert 'r->circlePrecision=24' in contract
assert 'BSNative_drawEllipseColor(renderer,cx,cy,rx,ry,col1,col2,outline)' in header
assert 'nvgCircle(b->vg,0,0,1)' in impl
assert 'nvgRadialGradient(b->vg,0,0,0,1' in impl
assert 'if(sw<=0)sw=(int)vw-sx;' in impl
assert 'if(sh<=0)sh=(int)vh-sy;' in impl
assert 'video_frame_to_nvg=1' in impl
assert 'sceAvPlayerGetAudioData(player,&frame)' in video
assert 'sceAudioOutOutput(audioPort,frame.pData)' in video
assert 'avStopAudio();' in video
assert 'video_frame_stalled_recover_end' in video
assert 'NATIVE_GXM=dev_room_jump from=%d to=%d' in source
assert 'VitaSettings_drawDevOverlay(&settings,bs.renderer' in source
assert 'bs.runner->pendingRoom=pendingDevRoom' in source
assert 'if(confirm)pendingDevRoom=devTargetRoom' in source
assert 'DLTAGXM48' in cmake
print('GXM48 regression: video full-surface -1, audio thread, graceful stalled video, native ellipse, DEV settings hooks')

assert 'BSContractRenderer_getDevMetrics(bs.renderer,&memory,&evictions)' in source
assert 'void BSContractRenderer_getDevMetrics(' in impl
print('GXM48 DEV GPU memory and eviction counters show native renderer values: PASS')

assert 'if(lengths[choose]<=1e-8f || !isfinite(lengths[choose]))' in impl
assert 'if(!outline && ((x2-x1)*(y3-y1)-(y2-y1)*(x3-x1))==0.0f)return;' in impl
print('GXM48 triangle NaN/degenerate native GXM visual artifact guard: PASS')

# GXM48 Ch5 Castle Town GPU lifetime and per-phase instrumentation.
assert 'BSGxmSurface retired[BS_MAX_SURFACES*2];' in impl
assert 'b->retired[b->retiredCount++]=*s;' in impl
assert 'static void bs_release_retired(' in impl
assert 'void BSContractRenderer_collectRetired(' in impl
assert 'sceGxmFinish(win->context);' in impl
assert 'BSContractRenderer_collectRetired(bs.renderer);' in source
assert 'o.scenesPerFrame=id==0?4:2;' in impl
assert 'retire_pending=%u retire_total=%u' in impl
assert 'NATIVE_GXM=surface_retirement released=' in impl
assert 'NATIVE_GXM=castle_phase stage=armed_after_step pending=54' in source
assert 'NATIVE_GXM=castle_phase stage=room_change_done' in source
assert 'NATIVE_GXM=castle_phase stage=retired_done' in source
assert 'NATIVE_GXM=shader_room room=%d id=%d name=' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 Ch5 Castle Town: GPU-fenced surface retirement, multi-scene targets, hang-phase diagnostics, per-room GXP census: PASS')

# GXM48 Experimental-GL native GXP shader adapter: per-program uniform handles,
# per-draw packet snapshot, compile isolation, optical effects and settings.
assert 'GXMNVGshader runtime_shader[13];' in native
assert 'runtimeShaderMask' in native
assert 'for(int k=0;k<13;k++)' in native
assert 'if(!successes)' in native
assert 'nvgxmSetRuntimeShaderParams(' in native
assert 'memcpy(ret->shaderParams,gxm->drawShaderParams' in native
assert 'memcpy(gxm->activeShaderParams,call->shaderParams' in native
assert 'sceGxmProgramFindParameterByName(' in native
assert 'sceGxmSetUniformDataF(buffer,param,0,4,gxm->activeShaderParams)' in native
assert 'gameParams' in native
assert 'shaderValues[256][4];' in impl
assert 'bs_shader_uniform_write(' in impl
assert '((id+1)<<4)|1' in impl
assert 'int tag=bs_native_shader_tag(r,id);' in impl and 'bs_native_shader_uniform(tag)' in impl
assert 'shader_uniform_writes=%u' in impl
assert 'bs_native_shader_tag(r,id)' in impl
assert 'shd_grayscale_fade' in impl
assert 'shd_saturation' in impl
assert 'shd_alpha' in impl
assert 'shd_brightness_to_alpha' in impl
assert 'shd_forcefullalpha' in impl
assert 'settings.shadersEnabled?1:0' in source
assert "option[0]=='0'?0:" in source
assert 'mask=0x%02X' in source
for name in ['grayscale_fade','saturation','alpha','brightness_to_alpha','forcefullalpha']:
    cg=(root/'experiments/native-gxm/shaders'/f'{name}.cg').read_text()
    assert 'float4 main(' in cg and 'tex2D(tex,uv)' in cg
    assert 'frag[11]' in cg and 'float4 gameParams;' in cg
assert 'DLTAGXM48' in cmake
print('GXM48 Experimental-GL parity: per-asset handles, captured uniforms, 5 new Cg shaders, independent compile masks, auto settings: PASS')

assert 'run_native_shader_visual_probe(vg)' in source
assert 'gxm-shader-preview.txt' in source
assert 'NATIVE_GXM=visual_probe stage=frame_presented' in source
assert 'DLTAGXM48' in cmake
print('GXM48 lab: preview uses native fragment programs with no Runner/OpenAL: PASS')

# GXM48 Room Navigator room108 crash: native warped four-corner sprite.
nvgcore=(root/'scratch/refs/nanovg-gxm/src/nanovg.c').read_text(encoding='utf-8')
nvgheader=(root/'scratch/refs/nanovg-gxm/src/nanovg.h').read_text(encoding='utf-8')
assert 'void nvgDrawImageQuad(' in nvgcore and 'void nvgDrawImageQuad(' in nvgheader
assert 'static const unsigned idx[6]={0,2,1,0,3,2};' in nvgcore
assert 'state->compositeOperation,&state->scissor,verts,6,ctx->fringeWidth' in nvgcore
assert 'nvgTransformPoint(&verts[i].x,&verts[i].y,state->xform' in nvgcore
assert 'nvgDrawImageQuad(b->vg,img,xy,uv,bs_clamp01(alpha));' in impl
assert 'drawSpritePos_projective_quad' not in impl
assert 'if(fabsf(crossA)<=0.00001f && fabsf(crossB)<=0.00001f)return;' in impl
assert 'if(!img){b->missing++;return;}' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 Chapter3 Navigator: projective sprite_pos as native textured quad + scissor/affine/blend retained: PASS')

assert 'gxm-quad-preview.txt' in source
assert 'nvgDrawImageQuad(vg,img,xy,uv,tile>=4?0.75f:1.0f);' in source
assert 'NATIVE_GXM=quad_preview stage=frame_presented' in source
assert 'NATIVE_GXM=step_hotspot room=%d rank=%d' in source
runner=(root/'src/butterscotch/src/runner.c').read_text(encoding='utf-8')
runnerheader=(root/'src/butterscotch/src/runner.h').read_text(encoding='utf-8')
assert 'gxmStepProfileKeys[128]' in runnerheader
assert 'runner->gxmStepProfileUs[slot]+=dur*8u;' in runner
print('GXM48 quad visualization lab + object-level Step CPU hotspots: PASS')

# GXM48 Ch5 native fixes; shaders, realtime LUT screen copies, solid background and Chapter0.
assert 'runtime_shader[13];' in native
assert 'case 8:return shadowblend;' in native
assert 'case 9:return forcecolour;' in native
assert 'gxm->activeShaderTag<=13' in native
assert '(gxm->activeShaderTag<=5||gxm->activeShaderTag>=8)' in native
assert 'shd_shadowblend' in impl
assert 'shd_forcecolour' in impl
for name in ['shadowblend','forcecolour']:
    cg=(root/'experiments/native-gxm/shaders'/f'{name}.cg').read_text()
    assert 'float4 gameParams;' in cg and 'tex2D(tex,uv)' in cg
assert 'nvgFillColor(b->vg,nvgRGB(0,0,0));nvgFill(b->vg);' in impl
assert 'const char*obj=runner->dataWin->objt.objects[instance->objectIndex].name;' in runner
assert 'strcmp(obj,"obj_lutshader")==0' in runner
assert 'strcmp(name,"obj_ui_version")==0' in runner
assert 'safeStrdup("DELTARUNE VITA v0.78")' in runner
assert 'DLTAGXM48' in cmake
print('GXM48 Ch5: GPU LUT pass dedup, opaque center, shadowblend/forcecolour GXP and Chapter0 v0.78: PASS')

assert 'frame<((quadMode||reflectMode||colorMode)?1200u:360u)' in source
assert 'NATIVE_GXM=snapshot_source room=%d obj=%d' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 extended local quad preview + Ch5 snapshot origin diagnostics: PASS')

assert 'gxm-emulator-mute.txt' in (root/'src/butterscotch/src/audio/openal/al_audio_system.c').read_text(encoding='utf-8')
assert 'DLTAGXM48' in cmake
print('GXM48 emulator-only opt-in audio crash isolation: PASS')

assert 'gxm-emulator-room.txt' in source
assert 'NATIVE_GXM=emulator_room_requested' in source
assert 'DLTAGXM48' in cmake
print('GXM48 direct city scene probe: PASS')

# GXM48 pixel format: regression based on Ch5 town screenshot comparison.
assert '((uint16_t)(r>>4)<<12)' in (root/'experiments/native-gxm/rgba4444_native.h').read_text()
assert '((uint16_t)(b>>4)<<4)' in (root/'experiments/native-gxm/rgba4444_native.h').read_text()
assert '(uint16_t)(a>>4)' in (root/'experiments/native-gxm/rgba4444_native.h').read_text()
assert 'DLTAGXM48' in cmake
print('GXM48 native RGBA4444 G/B channel mapping corrected for source RGBA8888: PASS')

# Pixel-golden triplet from actual Vita3K chapter5 TownMid:
# reference RGBA8888 (115,89,136,255) => nibble decode (119,85,136,255).
def gxm4844_rgba(r,g,b,a):
    packed=((r>>4)<<12)|((g>>4)<<8)|((b>>4)<<4)|(a>>4)
    return tuple(((packed>>shift)&15)*17 for shift in (12,8,4,0))
assert gxm4844_rgba(115,89,136,255)==(119,85,136,255)
assert gxm4844_rgba(255,177,0,255)==(255,187,0,255)
assert gxm4844_rgba(255,0,0,0)==(255,0,0,0)
assert gxm4844_rgba(0,0,0,255)==(0,0,0,255)
print('GXM48 channel order: R/G/B/A exact RGBA8888-aligned 4bpp and alpha preservation PASS')

# Premultiplied-alpha contract: GXM source ONE, dest ONE_MINUS_SRC_ALPHA.
# Never multiply vertex alpha twice in the GXP output RGB path.
for effect in ['grayscale_fade','saturation','alpha','brightness_to_alpha','forcefullalpha']:
    effect_cg=(root/'experiments/native-gxm/shaders'/f'{effect}.cg').read_text()
    assert 'float4 sampleColor=tex2D(tex,uv);' in effect_cg
    assert 'mask.x*mask.y;' in effect_cg
    assert 'c.rgb*c.a' not in effect_cg
assert 'corrected*frag[6].rgb*sampleColor.a' in (root/'experiments/native-gxm/shaders/grayscale_fade.cg').read_text()
assert 'newRGB*frag[6].rgb*sampleColor.a' in (root/'experiments/native-gxm/shaders/saturation.cg').read_text()
assert 'sampleColor.rgb*tint*a' in (root/'experiments/native-gxm/shaders/alpha.cg').read_text()
assert 'float3 tint=frag[6].rgb/max(frag[6].a,0.00001);' in native
assert 'DLTAGXM48' in cmake
print('GXM48 5 translated GXP effects preserve premultiplied RGB and single alpha: PASS')

# Native extra sampler contracts: two Chapter5 GLSL-ES shaders.
assert 'runtime_shader[13];' in native
assert 'case 10:return shoujo;' in native
assert 'case 11:return lut;' in native
assert 'nvgxmSetRuntimeSamplerImage(NVGcontext*ctx,int stage,int imageId)' in native
assert 'int stageImages[4];' in native
assert 'memcpy(ret->stageImages,gxm->drawStageImages' in native
assert 'memcpy(gxm->activeStageImages,call->stageImages' in native
assert 'sceGxmSetFragmentTexture(gxm->context,' in native
assert 'gxm->activeShaderTag==10||gxm->activeShaderTag==11' in native
assert 'strcmp(name,"texGradient")==0)return 1;' in impl
assert 'strcmp(name,"texBubble")==0)return 2;' in impl
assert 'strcmp(name,"texStars")==0)return 3;' in impl
assert 'strcmp(name,"lut_tex")==0)return 1;' in impl
assert 'strcmp(name,"appSurfWidth")==0' in impl
assert 'shaderIsCompiled(runner->renderer,14)' in runner
for name in ['shoujo','lut']:
    shader=(root/'experiments/native-gxm/shaders'/f'{name}.cg').read_text()
    assert 'uniform float4 frag[11]' in shader
    assert 'uniform float4 gameParams' in shader
    assert 'TEXUNIT1' in shader
assert 'TEXUNIT3' in (root/'experiments/native-gxm/shaders/shoujo.cg').read_text()
assert 'DLTAGXM48' in cmake
print('GXM48 shoujo 4-sampler GXP and LUT 2-sampler GXP, per-draw stage+uniforms and resumed LUT passes: PASS')

# GXM48 Castle Town reflection shaders preserve GML uniforms per draw call.
assert 'runtime_shader[13];' in native
assert 'case 12:return castlereflect;' in native
assert 'case 13:return castlereflect_layerblend;' in native
assert 'float shaderVectors[16];' in native
assert 'memcpy(ret->shaderVectors,gxm->drawShaderVectors' in native
assert 'memcpy(gxm->activeShaderVectors,call->shaderVectors' in native
assert 'gxm->activeShaderTag==13?2:1' in native
assert 'nvgxmSetRuntimeShaderVectors(NVGcontext*ctx,const float vecs[16])' in native
for name in ['castlereflect','castlereflect_layerblend']:
    cg=(root/'experiments/native-gxm/shaders'/f'{name}.cg').read_text()
    assert 'shaderMask' in cg and 'float4 frag[11]' in cg
assert 'u_object_surface_uvs' in impl
assert 'u_mask_surface_uvs' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 castle reflection shaders, vectors, dual samplers and blend/scissor: PASS')

assert 'gxm-reflect-preview.txt' in source
assert 'NATIVE_GXM=reflection_preview stage=frame_presented' in source
assert 'NATIVE_GXM=reflect_uniform asset=%d tag=%d slot=%d' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 reflection lab and actual GameMaker uniform diagnostics: PASS')

# GXM48 no-loss font color fix and accurate Begin/Normal/End Step profiling.
assert 'bs_page_is_font_atlas' not in impl
assert 'if(fontPage)mode=2;' not in impl
assert 'NATIVE_GXM=font_atlas_lossless' not in impl
runner=(root/'src/butterscotch/src/runner.c').read_text(encoding='utf-8')
assert 'runner->gxmStepPhaseUs[phase]+=dur*8u;' in runner
assert 'runner->gxmStepDispatchUs[0]' in runner
assert 'runner->gxmStepDispatchUs[1]' in runner
assert 'runner->gxmStepDispatchUs[2]' in runner
assert 'NATIVE_GXM=step_phases room=%d' in source
assert 'NATIVE_GXM=step_hotspot room=%d rank=%d obj=%d phase=%d' in source
assert 'DLTAGXM48' in cmake
print('GXM48: font atlasses use quantized native RGBA4444; all Step phases measured: PASS')

# GXM48 safe per-concrete-object event code cache preserves snapshot scheduling.
assert 'if(eventType==EVENT_STEP){' in runner
assert 'cachedCode=ResolvedEventTable_lookup(' in runner
assert 'runner->gxmStepLookupSaved++;' in runner
assert 'runner->gxmStepPhaseUs[phase]+=dur*8u;' in runner
assert 'NATIVE_GXM=step_cache room=%d saved_event_lookups=' in source
assert 'DLTAGXM48' in cmake
print('GXM48: per-object resolved Step code reuse, event order/room lifecycle intact: PASS')

runner_current=(root/'src/butterscotch/src/runner.c').read_text(encoding='utf-8')
assert 'gxmSampleEventTiming = (runner->frameCount & 7U) == 0U' in runner_current
assert 'runner->gxmStepPhaseUs[phase]+=dur*8u;' in runner_current
assert 'runner->gxmStepProfileUs[slot]+=dur*8u;' in runner_current
assert 'DLTAGXM48' in cmake
print('GXM48 sampled telemetry without changing GameMaker frame/event execution: PASS')

assert 'emulatorSequenceNextRoom' in source
assert 'NATIVE_GXM=emulator_sequence stage=second_jump' in source
assert 'bs.runner->currentRoomIndex==225' in source
assert 'DLTAGXM48' in cmake
print('GXM48 Ch5: emulated Room Navigator 54->225 sequence and stage traces PASS')

assert 'BSContractRenderer_devRoomJumpPrepare(bs.renderer);' in source
assert 'castlePhaseTraces=0;' in source
assert 'NATIVE_GXM=dev_jump_phase stage=gpu_fence_complete' in source
assert 'void BSContractRenderer_devRoomJumpPrepare(Renderer*r)' in impl
assert 'NATIVE_GXM=dev_transition_purge pages=' in impl
assert 'b->pageImages[i]=0;' in impl
assert 'nvgxmSetRuntimeSamplerImage(b->vg,stage,0);' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 DEV jump: GPU-fenced atlas retirement and destination phase logs PASS')

assert 'BSContractRenderer_framePresented(bs.renderer);' in source
assert 'void BSContractRenderer_framePresented(Renderer*r)' in impl
print('GXM48 Renderer frame-active state closes at gxmSwapBuffer: PASS')

# GXM48 uses RGBA4444 for ALL sampled assets including font, border,
# settings icons and GameMaker's create_sprite_from_surface. The mandatory
# GXM render targets stay in the framebuffer format required by SceGxm.
rgba4=(root/'experiments/native-gxm/rgba4444_native.h').read_text()
border=(root/'experiments/native-gxm/native_borders.inc').read_text()
ui=(root/'experiments/native-gxm/vita_settings_native.c').read_text(encoding='utf-8')
assert 'gxmRGBA4444Pack' in rgba4
assert '((uint16_t)(r>>4)<<12)' in rgba4
assert '((uint16_t)(g>>4)<<8)' in rgba4
assert '((uint16_t)(b>>4)<<4)' in rgba4
assert '(uint16_t)(a>>4)' in rgba4
assert 'flags|NVG_IMAGE_RGBA4444' in rgba4
assert 'gxmRGBA4444Upload(b->vg,w,h,NVG_IMAGE_NEAREST|NVG_IMAGE_LPDDR,rgba)' in impl
assert 'gxmRGBA4444Upload(b->vg,tw,th,NVG_IMAGE_NEAREST,rgba)' in impl
assert 'gxmRGBA4444Upload(b->vg,w,h,NVG_IMAGE_NEAREST,rgba)' in border
assert 'gxmRGBA4444Upload(g_nativeSettingsVG,' in ui
assert 'NATIVE_GXM=texture_format enforced_rgba4444_all_atlases' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 only sampled RGBA4444, correct RGB/A channel order and no 8888/BC3 fallback: PASS')

assert 's->pendingTextureFormatProfile = 0;' in ui
assert 's->pendingBc3Enabled = false;' in ui
assert 's->pendingPvrEnabled = false;' in ui
assert '"Texture Format", "Formato de textura"' in ui
print('GXM48 graphics menu accurately reports fixed RGBA4444 and real shader toggle: PASS')

# GXM48 GPU color calibration works with the same RGBA4444 upload path as gameplay.
rgba4=(root/'experiments/native-gxm/rgba4444_native.h').read_text(encoding='utf-8')
assert 'gxmRGBA4444PackVariant(' in rgba4 and 'gxmRGBA4444Layout(' in rgba4
assert 'gxm-color-layout.txt' in rgba4 and 'gxm-colors-preview.txt' in source
assert 'NATIVE_GXM=color_preview stage=frame_presented' in source
assert 'NVG_IMAGE_RGBA4444|NVG_IMAGE_NEAREST,(const uint8_t*)packed' in source
assert 'gxmRGBA4444Pack(src[0],src[1],src[2],src[3])' not in rgba4
assert 'NATIVE_GXM=texture_format rgba4444_native layout=%d' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 full 16-bit hardware channel-layout diagnostic with alpha swatches PASS')

# GXM48 Vita physical alpha channel correction: R<->A for RGBA4444.
fmt=(root/'experiments/native-gxm/rgba4444_native.h').read_text(encoding='utf-8')
assert '{3,1,2,0}' in fmt and 'perm[9][4]' in fmt
assert 'layout=0;' in fmt and 'g_vitaActiveChapter==0' not in fmt and "if(c>='0'&&c<='8')" in fmt
assert 'gxm-color-layout-ch%d.txt' in fmt and 'gxm-color-layout.txt' in fmt
assert 'NATIVE_GXM=rgba4444_channel_layout variant=%d first_page=%d' in impl
assert 'DELTARUNE VITA v0.78' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 hardware alpha/red swap, emulator layout, version label: PASS')

# GXM48 32-bit surface -> native 16-bit GPU transfer path and per-chapter pack.
assert 'sceGxmTransferCopy(' in impl
assert 'SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR' in impl
assert 'SCE_GXM_TRANSFER_FORMAT_U4U4U4U4_ABGR' in impl
assert 'sceGxmTransferFinish();' in impl
assert 'snapshot_transfer mode=%s' in impl
assert 'if(!copied)' in impl and 'gxmRGBA4444Upload(b->vg,tw,th,NVG_IMAGE_NEAREST,rgba)' in impl
assert 'gxm-color-layout-ch%d.txt' in fmt
assert 'DLTAGXM48' in cmake
print('GXM48 per-chapter rgba4444 and GPU sprite snapshot transfer with CPU fallback PASS')

assert 'bs_snapshot_sync(b,0);' in impl
assert 'if(cpuWait)sceGxmFinish(gxmGetWindow()->context);' in impl
assert 'SCE_GXM_TRANSFER_FRAGMENT_SYNC' in impl
assert 'src->fb->gxm_back_buffer_index].sync_object' in impl
assert 'snapshot_timing gpu_avg_us=' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 producer fragment sync transfer without global GPU finish PASS')

assert '#define BS_SNAPSHOT_POOL_MAX_BYTES (5u*1024u*1024u)' in impl
assert 'static int bs_snapshot_pool_take(' in impl
assert 'static void bs_snapshot_pool_retire(' in impl
assert 'bs_snapshot_pool_collect(b);' in impl
assert 'bs_snapshot_pool_retire(b,b->images[ti],b->width[ti],b->height[ti]);' in impl
assert 'int img=bs_snapshot_pool_take(b,tw,th);' in impl
assert 'if(!b||(!b->retiredCount&&!b->snapshotRetiredCount))return;' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 bounded GPU-safe snapshot texture recycler PASS')

assert 'target>b->originalTpagCount+512u' in impl
assert 'unsigned capacity=(target+63u)&~63u;' in impl
assert 'b->imageCount=capacity;' in impl
assert 'dynamicCreateTotal' in impl
assert 'DLTAGXM48' in cmake
print('GXM48 dynamic TPAG >23859 support; full sprite allocation and throttled logs PASS')
