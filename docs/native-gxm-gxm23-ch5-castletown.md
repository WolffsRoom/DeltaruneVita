# Native GXM23 — Chapter 5 Castle Town hang and renderer GXP coverage

## Evidence (real PSVita, GXM22)
- Physical log: `logs/chapter-log/geral/Ch5/native-gxm-probe.log`, final file timestamp October 13 2026 ~13:59.
- Source data.win Chapter 5 ROOM table identifies index **47 = `room_schooldoor`** and **54 = `room_dw_castle_town`**.
- Previous GXM22 reports stable ~25-39 FPS through Chapter 5 town, then slows to ~9.78 FPS, and final `NATIVE_GXM=fps ... fps_x100=675` = **6.75 FPS** immediately after `NATIVE_GXM=border room=room_dw_castle_town ...` and repeated fog on/off. There is no `shutdown` or renderer `unsupported=...` at the end; log alone cannot tell which function finally blocked.
- Earlier log shows 7 offscreen surfaces, many target switches, GXM shader_fallback=5634, main texture cache resident ~47 MiB / peak 52 MiB with ~21 evictions. The on-device memory figure does not account for all framebuffer/driver allocations.
- `shader_init requested=0 default=fixed_GXP` confirms experimental runtime shaders were **not enabled** on the physical Vita. Native backend's fog simply logs `tracked_gxp_pending`, so the original Castle Town fog/fragment effects were not rendered.

## Corrected in GXM23 (compiled, Vita hardware not yet verified)
1. GPU lifetime: `bs_destroy_surface` previously called `nvgDeleteImage`, `gxmDeleteFramebuffer`, and `gxmDeleteTexture` immediately during Runner's room change / surface resize. When the scene is open, NanoVG and the GPU may still reference those resources. The new deferred retirement queue retains original GXM framebuffer, sampler ID, and memory; it detaches their logical slots immediately but frees only **after nvgEndFrame/gxmEndFrame/gxmSwapBuffer and a sceGxmFinish fence**. Also fences queue exhaustion before resource reclaim. Renderer destruction collects retired surfaces safely.
2. GXM render-target scene reservation: offscreen framebuffer app surface id=0 now requests `scenesPerFrame=4` and secondary surfaces 2 rather than 1 everywhere; Castle Town uses several nested/adjacent passes per frame. No resolution, asset or texture-quality reduction.
3. Instrumentation: first 60 frames with Chapter5 `pendingRoom==54` or currentRoom54 emit `NATIVE_GXM=castle_phase` markers: begin, armed_after_step, step_done, views_done, room_change_done, nvg_flush_done, gxm_scene_end, swap_done, retired_done. The last completed stage identifies where an *unresolved future hang* occurred.
4. Shader census: `NATIVE_GXM=shader_room room=<index> id=<index> name=<source_name>` for every distinct shader used in each room. The old log capped `shader request=` globally at the first 20 (usually shd_shoujo ID55), obscuring Castle Town shader requests.
5. Metrics now include `retire_pending=<n> retire_total=<n>` and `NATIVE_GXM=surface_retirement released=<n> total=<n>` for direct validation.

## Shaders and performance: remaining work
- Important: GXM23 **does not claim to implement all GameMaker GLSL shaders**. The dedicated Cg/GXP workbench from GXM20 still supports only shd_grayscale and shd_invert experimentally, behind the separate Vita `gxm-native-shaders.txt` opt-in file and VitaShaRK `libshacccg.suprx` requirement.
- To explicitly test those two experimental native effects, create `ux0:data/deltarune/deltarunevita/gxm-native-shaders.txt` containing `1`, and ensure `ur0:data/libshacccg.suprx` is installed. Native GXM log shows success or safe fallback via `NATIVE_GXM=shader_init requested=1 ready=...`. Other Ch5 shaders, including shd_shoujo and all fog-depth effects, remain pending exact native GXP implementations. Enabling shaders DOES NOT resolve the Castle Town fog or promise image parity.
- No new physical FPS measurement or 60 FPS promise. On Castle Town, profile GPU scene submissions, textures, GameMaker events and view draws after resolving freeze. The new resource fences only occur when a surface is actually retired (e.g., transitions), not every frame.

## Build, regression, validation
- Native GXM23 VPK: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm23.vpk`, Title ID DLTAGXM23.
- Built via WSL Ubuntu VitaSDK with `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-gxm-wsl.ps1`.
- `python scripts/test-native-gxm-probe-contracts.py` passes all 73 renderer callbacks and new GXM23 resource-lifetime, 4/2 scene-target, diagnostics, per-room shader census assertions.
- Preserves GXM22 and Audit a3.

## Physical test instructions
- Install GXM23 separately, run Chapter 5 (same save) school-door -> Castle Town again. If transition succeeds, inspect dynamic borders, visual effects and actual FPS.
- If hangs again, copy **complete** `ux0:data/deltarune/deltarunevita/native-gxm-probe.log` to `logs/chapter-log/geral/Ch5/` and inspect the final `castle_phase` stage. Also preserve Vita crash dump if any.
- Compare the actual shader census for room54 against GL shader inventory `docs/native-gxm-shader-inventory.csv`; prioritize accurate GXP effects for the real Castle Town scene, rather than porting shaders never used there.
