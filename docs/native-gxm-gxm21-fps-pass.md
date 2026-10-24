# Deltarune native GXM21 — CPU/GXM renderer performance pass

## Physical Vita evidence (no fabricated improvement)
- Compared existing **GXM18 physical logs**, `logs/chapter-log/geral/Ch3/native-gxm-probe.log`. Chapter3 room 75 sample: step ~2444us, draw ~26945us, swap ~5600us.
- Chapter3 first playable scene `room_dw_couch_overworld_intro` (98): 8 GXM18 samples average step ~21181us, draw ~20092us, swap ~4252us.
- Detailed Runner profiler shows **events_us ~22000–24000us** in stable room98 portions. This is GameMaker VM script/event cost and is NOT addressed by renderer optimizations.
- Latest GXM20 didn't have a physical FPS log when this work started. No GXM21 FPS gains or 60 FPS are asserted without running on a physical Vita.

## Implemented CPU/GXM optimizations, no graphics quality switches
1. `bs_image_offscreen`: exact affine rectangular AABB via independent transformed edge vectors. Avoids four NanoVG point-transform calls and array/loop per sprite. Keeps rotation, shear, negative scale, conservative ±2px edge margin, finite-matrix fallback.
2. `bs_tpag_region`: pre-upload visibility check for unrotated world sprites is now reused. Previously identical rectangles were tested twice per sprite, increasing CPU work in complex scenes. Rotated sprites still get a post-rotation visibility test.
3. Three tile emitters — `bs_drawSpriteTiled`, `bs_drawTiledPart`, `bs_drawSurfaceTiled` — use an **inverse affine view transform** to calculate a conservative range of tile iteration indices. They retain exact tile stride, UV phase, draw order, original NanoVG clipping/scissor, texture source, tint, blend, alpha and scale. Sprite source trim and origin offsets enlarge the bounds to protect sprites partly overlapping edges. Degenerate camera inverse falls back to the old full iteration range.
4. `gxmnvg__findTexture`: caches verified texture ID+array index, not a raw pointer (the texture array can realloc; GC can recycle slots). Each cache hit still compares ID, preserving correct texture choice.
5. `gxmnvg__variantProgram`: remembers most recently used fragment-patcher variant. Verifies original fragment program ID, all blend factors, color mask and both blend equations; falls back to exhaustive scan on changes.
6. `gxmnvg__setUniforms`: skips redundant sceGxmSetFragmentTexture calls only when the same image is still bound in the same NanoVG flush. Cache resets at every frame/target flush; shader program selection and required uniforms continue normally.
7. Added metrics (every 300 frames): `tile_skipped`, `tile_drawn`, `tiled_calls` in render_metrics; `submit_cost flush_us`, `present_us`, and cumulative `lookups_hit`, `variant_hit`, `texture_bind_skipped` in new log line. This distinguishes GPU submission from sync/present and CPU Runner costs.

## Preserved visual features and settings
- No resolution decrease, render-scale adjustment, texture format change, downsampling, nearest filtering change, atlas omission, effect disabling, frame skipping, or script/collision behavior changes.
- Maintains current default original-byte -> RGBA4444 texture path; alternate Morton BC3 only through the existing explicit opt-in file.
- Chapter 3 gradient, sprite-font, alpha-test fallback, native border controller and Chapter5 MP4 bridge preserved from prior builds.
- GXM20 optional runtime GXP `shd_grayscale`/`shd_invert` settings remain optional, unchanged.

## Reproducibility
- New stand-alone VPK: `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm21.vpk`, title ID `DLTAGXM21` (preserves GXM19/GXM20, Audit a3).
- Docker Desktop in user PC couldn't start (`com.docker.service` stopped; Start-Service permission denied). Compiled instead with **WSL Ubuntu VitaSDK**, CMake+Ninja. Added fallback PowerShell script: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-native-gxm-wsl.ps1`.
- Full existing `python scripts/test-native-gxm-probe-contracts.py` passes all 73 callbacks and new hotpath assertions.
- `python scripts/test-gxm21-lossless-visibility.py`: seeded 3,000 randomized affine scene cases; 810,604 candidate tiles, 395,834 mathematically skipped, with zero visible convex-rectangle losses by Separating Axis Theorem. 25,000 randomized affine AABB comparisons pass. This is an algorithm proof/simulation, not a measured hardware draw-time improvement.
- VPK validated as ZIP, CRC pass and title ID recognized. Hardware gameplay/render equivalence and FPS must still be tested.

## Real-hardware benchmark requested
- On GXM21 Chapter 3 start New Game and remain at least 30 seconds in `room_dw_couch_overworld_intro` (98). Also check Chapter3 menu (75), room 103, room 106, and Chapter5 exit home to town.
- Save `native-gxm-probe.log` and compare `frame_cost step_us`, `draw_us`, `swap_us`, `submit_cost flush_us`, `present_us` for the **same room and similar camera/game state** against GXM18.
- `runner_profile events_us` should remain unchanged by render-only optimizations; high events_us is the next independent VM optimization workstream.
- Inspect visual differences at room/surface edges, tiled backgrounds, scrolling surfaces, palettes, borders, inventory, Chapter5 movie and shaders. Do not interpret test as GPU parity guarantee.

## Potential next performance wins (not implemented without validation)
- Cache exact clipping/scissor state per draw and only update GPU when changed; avoid modifying state order for blend effects.
- Separate GPU state/vertex geometry batching for sprite quads with varying UVs/uniforms, which requires dedicated native GXP and careful preservation of draw order and alpha blending.
- Profile expensive Chapter3 room98 GameMaker event scripts, since 22–24ms per step cannot be fixed solely in the GXM renderer.
