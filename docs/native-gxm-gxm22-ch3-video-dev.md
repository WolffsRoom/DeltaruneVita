# Deltarune Native GXM22 — Chapter 3 hardware feedback (2026-10-13)

## Evidence from real Vita GXM21 logs
- Ch3 log located `logs/chapter-log/geral/Ch3/native-gxm-probe.log`, latest 2026-10-13 12:10.
- GXM21 `tennaIntroF1_compressed_28.mp4`: path resolved, `sysmodule_ready`, `open_ok`, `first_frame` (640x480), then captions and room189 persisted for ~20,000 frames. No `end_of_stream` and movie remained invisible/inaudible.
- In GXM21 first playable room98 ~22-25ms events_us and ~16-29ms draw_us: the previous shader/quad CPU optimizations have not demonstrated a reliable 60 FPS improvement. Existing 15-25 FPS in demanding scenes is still possible.

## Native GXM22 fixes
1. **Video surface**: GameMaker `draw_surface` calls `Renderer::drawSurface` with `sw=-1` / `sh=-1` meaning full source. Previous special native movie ID handler erroneously required `sw>0 && sh>0`, quietly discarding every movie frame. Now resolves these sentinels using actual decoded width/height and applies original scale/tint/transform.
2. **MP4 audio path**: added separate `sceAvPlayerGetAudioData` worker with native `sceAudioOut` output, port selection BGM then VOICE. It is started at movie-open and joined before AvPlayer destruction. Original OpenAL audio remains. This is **experimental** and unverified for audio-out port contention, sync and frame pacing.
3. **Hung video**: records `frame_progress` periodically. If playback was started and no advancing video frame is produced for 10 seconds, posts `video_end` so GML's async video handler can continue instead of trapping users forever on a black screen. The actual cause of stalling requires hardware confirmation.
4. **Round lights**: native renderer originally left GameMaker `circlePrecision=0`, forced to 4 segments by shared clamp, unlike ModernGL's 24. Initializes to 24; native circle/ellipse draw now uses NanoVG curved geometry and a single radial color interpolation instead of a fan of 4 to 24 individually approximated gradient triangles. This removes square 'circles' and likely removes triangular seams in radial lighting while reducing draw calls.
5. **Room Navigator / Debug DEV**: original Game Settings checkboxes were exposed, but GXM `main.c` never consumed them. Wired D-pad stepping ±1 room, L+R? NO: **L+X to teleport** per playable original, with draw order and deferred GPU-safe transition after frame/present; no control bleed into GameMaker. Calls original `VitaSettings_drawDevOverlay` for room list, FPS, step timing, texture-cache bytes and evictions. Only active when `devmode=1` is set in shared configuration and DEV settings enabled.
6. Preserves GXM21 native texture caching, culling, shader opt-in, custom fonts and Chapter5 border/video experiments.

## Known limitations
- Still uses `drawTriangle_vertex_gradient` linear 2-stop approximation for arbitrary three-color triangles outside the circle path. Any separate black triangular artifacts should be photographed and the room identified; exact multi-vertex interpolation needs a native vertex-color shader.
- The Chapter3 `room_dw_couch_overworld_intro` bottleneck in VM `events_us` ~22-25ms is not fixed; even perfect GPU throughput cannot guarantee 60 FPS with those event costs.
- MP4 color fidelity and PCM audio have not been physically verified. A decoded frame's existence in a log does not establish correct image or sound. New audio thread success, sampler data and EOF need physical test.
- If the player stalls after receiving a first frame, a 10s no-frame fallback deliberately dispatches `video_end`. This avoids a permanent hang but is not a substitute for correct continuous video playback.
- Actual version v0.78-gxm22; previous VPKs and Audit a3 untouched.

## New logs to inspect
- `NATIVE_GXM=video step=first_frame` and `NATIVE_GXM=video draw_surface video_frame_to_nvg=1`: confirm decoder frame was handed to game and drawn.
- `NATIVE_GXM=video step=audio_port_open|audio_first_output|audio_port_unavailable|audio_output_failed` for audio output.
- `NATIVE_GXM=video step=frame_progress` every 120 delivered frames, `end_of_stream` or `video_frame_stalled_recover_end`.
- `NATIVE_GXM=dev_room_jump from=<room> to=<room>` for navigator.
- `NATIVE_GXM=frame_cost` and `runner_profile` for comparative FPS.

## Builds and test
- Compiled via `scripts/build-native-gxm-wsl.ps1` using Ubuntu/WSL VitaSDK (Docker Desktop was previously unavailable).
- `python scripts/test-native-gxm-probe-contracts.py`: PASS 73 renderer callbacks, existing regressions and GXM22 video/DEV/ellipse contracts.
- Native VPK in `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm22.vpk`, Title ID `DLTAGXM22`.

Additional triangle guard: nonfinite vertices and degenerate triangle areas no longer reach the approximate NanoVG gradient, and zero-length color-gradient edges use a finite fallback to avoid undefined fragment coefficients. This does NOT implement exact three-color vertex interpolation.
