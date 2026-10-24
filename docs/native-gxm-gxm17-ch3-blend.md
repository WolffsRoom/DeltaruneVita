# GXM17 — Chapter 3 Blend Crash Fix

## Hardware reproduction (GXM16)
Physical PSVita log: `logs/chapter-log/geral/Ch3/native-gxm-probe.log`.
Dump: `psp2core-1791862700-0x0000ea3707-eboot.bin.psp2dmp`.
First Room 1 -> 243 -> 75. DataWin parser, VM, renderer initialization and first gameplay frames passed.
Last recorded cause: `NATIVE_GXM=runner_renderer unsupported=blend_subtract`.
This line originates at the explicit `bs_unsupported("blend_subtract")` abort in the experimental renderer.
The Ch3 GXM16 log confirms RGBA4444 in use (`rgba4444=9`, `bc3=0`), so this particular abort is not evidence of a texture-format issue.

## GXM17 native implementation
Removed the deliberate aborts for blend_subtract, blend_min and blend_max.
Matched the playable `src/butterscotch/src/gl_common/gl_common.c` equations and factors:
- `bm_normal`: GXM ADD; preserve existing native NanoVG SOURCE_OVER.
- `bm_add`: GXM ADD; preserve existing native NanoVG LIGHTER.
- `bm_subtract`: GXM ADD; source ZERO; destination ONE_MINUS_SRC_COLOR.
- `bm_reverse_subtract`: GXM REVERSE_SUBTRACT; source SRC_ALPHA; destination ONE.
- `bm_min`: GXM MIN; source ONE; destination ONE.
- `bm_max`: GXM ADD; source SRC_ALPHA; destination ONE_MINUS_SRC_COLOR.
Extended `experiments/native-gxm/nanovg_gxm_native.h` blend variants to include colorFunc and alphaFunc.
The per-NVG-call blend equation and factors are recorded when the call is issued, and the shader patcher creates matching fragment program variants upon flush.
Unknown blend enum values produce a diagnostic and source-over fallback, not an intentional crash.
New log event `NATIVE_GXM=blend_mode mode=<enum> native_gxm=equation_and_factors` (one record per blend mode).
Original VitaGL source and existing GXM16/Audit a3 VPKs remain unchanged.

## Validation scope
Static regression checks: 73 renderer callbacks and all prior GXM16 tests plus targeted GXM17 GLCommon blend mapping.
Compile using `scripts/build-native-gxm-probe.ps1` (creates a separate Title ID `DLTAGXM17`).
Real Vita Chapter3 scene rendering remains unverified until next hardware test. No guarantee of a crash-free entire chapter or pixel-perfect visual parity.

## Test
Install GXM17, launch Chapter0 -> Chapter3, navigate past previously failing scene after room index 75.
Keep `gxm-texture-mode.txt` absent or `0` for safe RGBA4444.
Expect `NATIVE_GXM=blend_mode ...` instead of `unsupported=blend_subtract`.
Check subsequent callbacks, colors, transparency and crashes; review `logs/chapter-log/geral/Ch3/` dump on failure.
