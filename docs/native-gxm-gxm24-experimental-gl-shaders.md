# Deltarune Native GXM24 — Experimental-GL shader parity sprint

## Source material and architecture
- Reference `src/vita-probe/source/playable_main.c`: Experimental-GL selects programmable shader policy inside audited Legacy-GL; the renderer does not have a separate graphics backend.
- Reference `src/butterscotch/src/gl/gl_renderer.c` (`ensureShaderCompiled`, `glGpuSetShader`, `glShaderGetUniform`, `glShaderSetUniformF`, `glTextureSetStage`): embedded GLSL-ES per DataWin shader, main texture slot0, custom samplers follow, custom uniform locations per shader, GL state captured during batch flushing. GXM24 adapts the same contract to explicitly compiled Cg/GXP through `vitaShaRK` and native NanoVG-GXM.
- Reference `third_party/vitaGL-nosplash/source/custom_shaders.c`: `shark_compile_shader_extended`, `sceGxmShaderPatcherRegisterProgram`, `sceGxmProgramFindParameterByName`, native uniform buffers. GXM24 uses the same GXP compilation and parameter APIs but **does not link VitaGL**.

## New native-GXP shader variants
- Retains grayscale and invert from GXM20 and adds the following five GameMaker GL-ES effects translated to Cg:
  - `shd_grayscale_fade`, Ch5 SHDR10, `fade` scalar; luminance 0.21/0.72/0.07, grayscale/true-color interpolation
  - `shd_saturation`, Ch5 SHDR32, `u_desaturation` scalar; original 0.299/0.587/0.114 weights
  - `shd_alpha`, Ch5 SHDR33, `alpha` scalar; uses min(pixel alpha, user alpha)
  - `shd_brightness_to_alpha`, Ch5 SHDR30; RGB brightness maps to transparency
  - `shd_forcefullalpha`, Ch5 SHDR37; nonzero pixel alpha forced to full
- Each uses native NanoVG paint/scissor transforms, `gm_BaseTexture` on GXM sampler stage 0 and premultiplied-alpha output. Standalone source lives under `experiments/native-gxm/shaders/*.cg`; compiled program C-string equivalents are in `nanovg_gxm_native.h`.
- Shader tags 1–7 map by shader name, not GameMaker asset ID. Seven shaders correspond to 11 DataWin entries in Chapters 2–5. Status inventory `docs/native-gxm-shader-inventory.csv` marks these as experimental and unverified in real hardware.

## GameMaker uniform implementation
- Mirrors Experimental-GL asset-specific uniform lookup for the three scalar-program effects. Native handles encode asset index and uniform slot and persist values in `BSGxmRenderer::shaderValues[256][4]`.
- On `gpuSetShader`, chosen shader ID and its persisted uniform values are passed to NanoVG-GXM. Each queued `GXMNVGcall` copies both the shader tag and four float values. On flush, the backend switches fragment GXP and writes captured `gameParams` via `sceGxmSetUniformDataF` in the fragment default buffer. Later `shader_set_uniform_f` changes cannot modify already queued sprites.
- `shaderGetUniform` returns -1 for unsupported names or shaders not successfully compiled. `shaderGetSamplerIndex`/`textureSetStage` remain unimplemented for custom auxiliary texture bindings; this is not a general GameMaker programmable shader runner yet.
- Each GXP compiles independently: `runtimeShaderMask` indicates which succeeded. Shader compilation failure falls back to the existing fixed fragment program for only that shader, rather than disabling all.

## Enabling and physical limitations
- On startup, shared Game Settings `shadersEnabled` now determines whether to try native GXP. Optional `ux0:data/deltarune/deltarunevita/gxm-native-shaders.txt` containing `0` forces it off or `1` forces it on. With no file, Game Settings controls the choice.
- `ur0:data/libshacccg.suprx` is still a prerequisite. It cannot be redistributed in this project. With no module or unsuccessful GXP compilation, fixed GXM fallback remains. Startup log reports `NATIVE_GXM=shader_init requested=1 ready=<0|1> mask=0x.. result=... source=...`.
- **On-device shader compilation and visuals have NOT been tested**. Host VitaSDK C compilation does not compile runtime Cg or guarantee pixel parity. Settings toggling may require restarting the app; standalone file override is read only at boot.
- Complex Chapter5 shaders `shd_shoujo` (ID55), `shd_castlereflect_layerblend` (ID6), `shd_tower` (ID8), CRT, LUT, palettemapping, and custom fog remain pending. They require extra sampler stages, more uniform types, native vertex state, shaders for non-textured primitives, or accurate GLSL coordinate semantics. Candidate `shd_shoujo.candidate.cg` from GXM20 is still research-only.
- GXM23 Castle Town hang resource-lifetime/diagnostics, GXM22 native video+DEV experiments and GXM21 renderer fastpaths preserved. No new physical FPS measurement; neither 60FPS nor full shader fidelity is claimed.

## Build and regression
- Separate VPK `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm24.vpk`, Title ID `DLTAGXM24`. Existing GXM23, GXM22 and Audit a3 not overwritten.
- Build with `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-gxm-wsl.ps1` from repository root (Ubuntu/WSL VitaSDK).
- Run `python scripts/test-native-gxm-probe-contracts.py` to check original 73 renderer callbacks, optional audio/video, CastleTown deferred GPU surface lifetime, 7 GXP program IDs, captured per-draw uniforms, GameSettings and Cg sources. All static tests, not Vita runtime tests.

## Recommended Vita test
- Try Chapter5 with shaders enabled, inspect log `shader_init ... ready=1 mask=0x..` and `shader_effect ... gxp=...`, `shader_uniform_writes=<n>`; verify color/alpha effects where invoked. Mask bit0..bit6 represent grayscale, invert, grayscale_fade, saturation, alpha, brightness_to_alpha, forcefullalpha respectively.
- Reproduce Castle Town with the GXM23/GXM24 stage markers; if freeze occurs, the LAST `NATIVE_GXM=castle_phase ...` stage matters more than raw FPS.
- If failures or visual corruption happen, set `gxm-native-shaders.txt` to `0` to isolate shader adaptation from the renderer/room crash, without replacing VPK.
