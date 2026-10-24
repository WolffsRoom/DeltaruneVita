# Native GXM19 — Hardware GXM18 crash analysis / Audit a3 / border & video bridge

## Incoming physical Vita evidence (2026-10-13)
- Audit `logs/chapter-log/geral/summary.log` reports `GXM_AUDIT=v0.78-audit3`, all chapters scanned, **1 warning on Chapter2 page_414.bc3.pvr missing** on Vita: `bc3=414 missing=1`. PC preparation was previously corrected, but installed Vita data is still old. Copy the base Ch2 PVR page 414 to `ux0:data/deltarune/deltarunevita/chapter2/pvr/page_414.bc3.pvr`. Offline source checks also found TPAG 9557 reading beyond original 512x256 source, which remains an upstream asset inconsistency.
- GXM18 Ch3 physical log `logs/chapter-log/geral/Ch3/native-gxm-probe.log` ends `unsupported=drawTriangle_vertex_gradient` in room index 103. No video-open event occurred at that fault.
- GXM18 Ch5 physical log `logs/chapter-log/geral/Ch5/native-gxm-probe.log` ends `unsupported=switch_target_inside_view` in room index 16 (exit from home), 13 -> 16; native shader #55 requested repeatedly and not executed.
- `Chapter3 room 98 = room_dw_couch_overworld_intro`: GXM17 median 22.1 FPS in logs; GXM18 median 22.0 FPS. Other rooms may have higher FPS. Culling is present but not proof of improved first-room performance.

## Implemented in GXM19
- Chapter 3: native `drawTriangle` no longer aborts for distinct per-vertex colors. Uses a single-pass linear gradient along the strongest vertex color difference, **an approximation**, not exact 3-corner interpolation. Multi-corner rectangle and gradient lines also have nonfatal linear approximations.
- Chapter 5: `bs_switch_target` now saves active view/GUI parameters, ends the old NanoVG/GXM target and restores the camera/scissor/transform after switching. Avoids deliberate `switch_target_inside_view` abort.
- Surface tint, surface tiled tint and `drawSurfaceColor` four-color call now fall back safely without abort. Vertex-gradient on a textured surface is averaged until custom GXP exists.
- Background outside the game viewport is pure black instead of dark purple.
- Border source selection follows live `obj_border_controller` on Chapters 1–5 when Better Borders is not overriding; uses `_border_image`, `_border_image_temp`, `_border_image_temp_alpha`, `_border_crossfade`, `overlay_alpha`, `overlay_color`, `border_alpha`, `_border_red_alpha`; preserves 24-frame art crossfade and some Chapter 5 garden/pink/Flowery state flags. Full dynamic color overlays and specialized mask effects may still differ from playable VitaGL.
- `NATIVE_GXM_VIDEO` experimental path: compiled `SceAvPlayer` H.264 decoder adapter using PHYCONT buffers and CSC1 native GXM texture sampling; GML `video_open`, `video_draw`, status, pause, resume, looping, async `video_start/end` now go to native adapter instead of the desktop dummy surface. Deferred player close to post-GXM-present. Video image, orientation, performance and sound MUST be verified physically.
- IMPORTANT movie audio: this initial GXM decoder bridge **does not implement MP4 AAC audio output yet**, while external Chapter5 OGG can still play through regular OpenAL. It is not equivalent to the mature VitaGL movie subsystem; do not promise audio or chapter video parity.

## Shaders — not yet GXP-ported
- All SHDR GLSL-ES source and metadata exported to `docs/native-gxm-shader-inventory.csv`: 133 across Chapters 2–5, 66 in Chapter 5.
- Chapter5 shader index 55 is `shd_shoujo` ('Sara's Magical Girl Shader') with extra samplers `texGradient`, `texBubble`, `texStars` and dynamic uniforms `time`, `appSurfWidth`. This demands native GPU multi-texture sampling + compiled GXP fragment program. Fixed NanoVG shader cannot faithfully implement it; still running fallback and **not marked implemented**.
- Compile all GXP or port effects by priority using Modern-GL SHDR source and GameMaker uniforms; no source availability implies ready binary GXP.

## Tests and scope
- Build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-gxm-probe.ps1`
- VPK: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm19.vpk`; Title ID `DLTAGXM19`; existing GXM18 and Audit a3 preserved.
- Automated tests validate 73 renderer vtable callbacks, diagnostic paths, safe culling, native border source and AvPlayer wiring. Tests are **static/compile**, not physical image equivalence.
- Check after hardware: Ch3 room 98 FPS, Ch3 gradient triangles, Ch5 exit-home to town, TV/garden borders, Chapter3 and Chapter5 MP4 playback frame/color/orientation, sound and EOF.

## Log keywords for next hardware run
- `NATIVE_GXM=surface_switch inside_active_pass restore_camera=1`
- `NATIVE_GXM=drawTriangle_vertex_gradient mode=native_linear_approx`
- `NATIVE_GXM=video step=open_ok|first_frame|end_of_stream|prepare_timeout`
- `NATIVE_GXM=runner_profile` and `NATIVE_GXM=render_metrics ... offscreen_culls=...`
- `NATIVE_GXM=shader request=55 backend=fixed_gxm_untranslated` persists pending shader migration.
