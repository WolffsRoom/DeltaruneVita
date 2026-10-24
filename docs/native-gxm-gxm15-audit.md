# Deltarune Vita — Native GXM v0.78-gxm15 audit (2026-10-12)

Scope: experimental native-GXM build; real Vita validation remains pending.

## Crash diagnosed
The last gxm13 Castle Town log ends with NATIVE_GXM=runner_renderer unsupported=fog.
Crash dump: psp2core-1791857228-0x0000922b95-eboot.bin.psp2dmp.
Chapter2 reaches rooms 60/61, loads border_dw_castletown_0.png, and eventually hits the fatal fog callback.
gxm15 tracks fog enable/color instead of aborting. Native per-pixel fog is not yet equivalent to ModernGL.

## Five-chapter BC3 / shader audit
| Chapter | BC3 PVR valid | BC3 MiB | SHDR shaders |
|---|---:|---:|---:|
| 1 | 192 | 44.66 | 0 |
| 2 | 414 | 144.71 | 5 |
| 3 | 397 | 159.45 | 21 |
| 4 | 838 | 188.69 | 41 |
| 5 | 1017 | 265.52 | 66 |
| Total | 2858 | 803.03 | 133 |
All PVR files pass header/format/length checks using scripts/audit-native-gxm-chapters.py.
Offline resource validation does not mean hardware graphics fidelity or gameplay was validated.

## Implemented in gxm15
- Replace per-TPAG texture uploads with shared GXM texture per DataWin TXTR atlas page.
- Offset sprite, bitmap glyph and texture_get_uvs coordinates by the TPAG source rectangle.
- Read chapter1–5/pvr/page_###.bc3.pvr; validate complete PVR v3 and upload UBC3_ABGR directly.
- If BC3 absent, decode source TXTR then upload native 16-bpp U4U4U4U4_ABGR; fall back to RGBA8888 if required.
- Reuse src/vita-probe/source/vita_gpu_rules.c: 48 MiB regular / 56 MiB selected Chapter5 scenes.
- Prevent eviction of pages used in the last eight frames. Eviction only after GPU submission and completion.
- Telemetry: bc3, rgba4444, rgba8888, cache_mib, peak_mib, evictions, fog_calls, shader_fallback.
- Fog and custom-shader calls no longer trigger deliberate abort; their exact visual effects are NOT implemented.
- Full Game Settings UI from gxm13 remains, graphics menu now describes automatic BC3/RGBA4444.
- Prior gxm14 and playable VitaGL remain untouched.

## Work still required
1. Translate and offline-compile the 133 SHDR shaders to Vita GXP, bind GML uniforms and texture samplers.
2. True per-texel alpha test, complex blend/min/max/subtract, projective primitives, vertex buffers, custom matrix paths.
3. GXM sprite batching and streaming profile against Chapter2 ~45ms render cost; 60 FPS is not guaranteed.
4. Chapter3 gameshow shader effects; Chapter4 large scene assets; Chapter5 Flowey video, garden effects and extensive shader set.
5. Long-running physical Vita stability and correct PNG/BC3/GXM color rendering need verification.

## Suggested test
1. Install gxm15; Chapter0 -> Chapter2 -> Castle Town. Verify fog no longer aborts.
2. Copy native-gxm-probe.log; check bc3 > 0, rgba4444 fallback, cache/evictions, shader requests and frame_cost.
3. Verify ux0:data/deltarune/deltarunevita/chapter2/pvr/ is installed; without it, fallback uploads are expected.
4. Test Chapters 1,3,4,5 separately. Send first failing room and any new psp2dmp.
