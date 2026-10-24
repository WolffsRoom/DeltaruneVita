# Deltarune native GXM20 — first shader workbench

## Actual changes (not just documentation)
- Native NanoVG GXM `GXMNVGcall` now captures the current GameMaker shader tag (`drawShaderTag`) when each draw is queued. Flush replays the tag per draw. Shader reset selects tag 0. This avoids applying a later shader globally to earlier commands.
- Two Cg pixel effects are compiled to native GXP via `vitaShaRK` on hardware **only when explicitly enabled**: `shd_grayscale` (matching original coefficients 0.21,0.71,0.07) and `shd_invert` (RGB inversion with original alpha). Both use the pinned fixed NanoVG vertex GXP, frame paint transform and scissor-mask interface.
- Chapter mappings are by actual `Shader.name` in DataWin rather than global shader ID: Ch2 ID3 grayscale, Ch3 ID17 grayscale / ID20 invert, Ch4 ID33 grayscale, Ch5 ID51 grayscale / ID62 invert.
- Shader query `shader_is_compiled` returns true for only those two names **after native shader compilation succeeds**; unsupported shaders continue to return false and use the existing fixed GXM fallback.
- GXM20 is standalone (title ID `DLTAGXM20`) and preserves GXM19 and Audit a3.

## Opt in on physical Vita
1. Install the GXM20 VPK.
2. Ensure the own extracted `libshacccg.suprx` is already installed at `ur0:data/libshacccg.suprx` (same prerequisite as the VitaGL dynamic shader backend; do not ship it in the VPK).
3. To test the new shader path, create the file `ux0:data/deltarune/deltarunevita/gxm-native-shaders.txt` with one character `1`.
4. Re-launch the app. The two Cg programs compile once at bootstrap, before starting GML drawing. This can take noticeable time on real hardware.
5. To revert to safe fixed GXM, remove the file or set it to `0`. Without explicit opt-in, the compiler and module are not initialized.

## What logs prove
- `NATIVE_GXM=shader_init requested=0 default=fixed_GXP`: experimental compiler not called.
- `NATIVE_GXM=shader_init requested=1 ready=1 result=0x00000000 backend=vitaShaRK_gxp`: compiler initialized and both programs registered; **not proof of pixel-perfect display**.
- `NATIVE_GXM=shader_init requested=1 ready=0 ...`: shader compilation failed; game falls back to fixed GXM safely.
- `NATIVE_GXM=shader_effect id=<id> gxp=shd_grayscale|shd_invert mode=runtime`: shader selected for NanoVG texture-fill draw calls.
- `NATIVE_GXM=shader request=55 backend=fixed_gxm_untranslated`: Chapter5 `shd_shoujo` is still pending.

## Shader sources and next priority
- Runtime Cg programs: `experiments/native-gxm/shaders/gray.cg`, `invert.cg`.
- Cg reference translation for Chapter5 shader55: `experiments/native-gxm/shaders/shd_shoujo.candidate.cg`. **NOT compiled, linked, or claimed implemented**.
- `shd_shoujo` needs multi-sampler binding (base texture stage0, gradient stage1, bubble stage2, stars stage3), extra uniforms (`time`, `appSurfWidth`, inferred viewport height), correct `gl_FragCoord`-equivalent placement and render-target state. The existing `bs_textureSetStage` is still a stub; only after that infrastructure is implemented can the Chapter5 opening effect be faithful.
- Remaining >120 distinct Chapter2–5 shader instances still use old fixed GXM and are not implemented.
- `docs/native-gxm-shader-inventory.csv` marks only the six IDs of the two simple effects as `EXPERIMENTAL_RUNTIME_GXP_OPT_IN_UNVERIFIED`; all others remain `NOT_IMPLEMENTED`.

## Performance and hardware caveats
- `vitaShaRK` requires the firmware shader module and taiHEN, but GXM20 still makes draw calls directly through sceGxm; this is not a VitaGL backend.
- There is no host-side GXP compiler integrated yet. Cg source correctness and compiled fragment program quality cannot be established by a C compiler or Python tests alone.
- The initial Cg program applies to NanoVG textured *fills*; glyph-text paths, four-sampler effects, custom vertex transforms and post-process surfaces are not covered.
- The first-room low-FPS problem in Chapter3 has not been measured as improved by shaders.
- Previous GXM19 gameplay/borders/video remains experimental; these changes do not establish real-hardware stability.

## Build/validation
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-gxm-probe.ps1`
- `python scripts/test-native-gxm-probe-contracts.py`
- VPK `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm20.vpk` (Title ID `DLTAGXM20`).
