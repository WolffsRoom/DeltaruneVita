# Startup, Frame Pacing, and Room Streaming Design

## Goal

Reduce the delay between the opening video and Chapter 0, make 30 FPS and 40 FPS modes more consistent, and reduce room-transition and battle stalls on the PlayStation Vita. CapUnlocker may supply the fourth CPU core, while IOStaging is evaluated as an optional system-level accelerator. The game must remain functional when neither plugin is installed.

Success means:

- Chapter 0 parsing overlaps the opening video and does not add a second blocking parse after the video.
- Missing CapUnlocker falls back to the normal affinity policy without changing game behavior.
- 30 FPS and 40 FPS pacing avoids catch-up bursts and reduces frame-time variance when the frame workload fits the selected budget.
- Room payload reads and texture preparation move off the render thread where safe.
- VitaGL and GXM calls, GameMaker events, live VM state, surfaces, and texture uploads remain on the main/render thread.
- Final-battle improvements are measured separately as CPU staging gains and GPU rendering gains.
- IOStaging is never bundled or required; it is tested through an explicit Vita A/B procedure.

## Constraints and Current Evidence

The existing startup plays the opening video before `DataWin_parse()`. This makes the video and Chapter 0 parsing sequential even though parsing does not need the renderer. The parser already supports lazy rooms, textures, and audio, so a background parse can hide useful work without eagerly allocating every asset.

The existing frame limiter advances a microsecond deadline, sleeps until one millisecond before it, and then spins. Late frames reset the deadline. This avoids a catch-up burst, but the fixed spin margin wastes CPU time and does not compensate for the measured overshoot of Vita sleep calls.

Room changes currently free the old payload and synchronously read the next payload through a shared `FILE *` before creating the room. Texture decoding and uploads can also produce visible stalls. A worker must not use that shared file handle or mutate a live room.

Flowey's final battle remains primarily constrained by rendering and GPU synchronization. Existing logs show render times far above the CPU frame budget and substantial surface/VRAM pressure. A fourth CPU core can reduce file-read, decode, and preparation spikes, but it cannot execute VitaGL surface draws or make the base GPU workload disappear.

## Architecture

### One bounded background-work service

Add a single Vita background-work service rather than creating independent ad hoc workers. It owns one queue, uses the fourth core through the existing CapUnlocker-aware thread policy when available, and falls back to the standard permitted cores. Only one memory-heavy job runs at a time.

Jobs carry a generation number and cancellation flag. Changing chapters, returning to the title, or superseding a room transition invalidates older generations. Results are immutable buffers or detached parsed objects; the main thread explicitly adopts them. The service never invokes VitaGL, GXM, GameMaker events, audio output, or methods that mutate the live VM.

The service has an 8 MiB total staging budget and admits no single job larger than that budget. Texture work normally stages one decoded page at a time. Allocation failure or memory pressure disables the current background operation and falls back to the existing synchronous path.

### Chapter 0 preload during the opening video

Resolve the Chapter 0 `data.win` or selected mod path before starting the opening video. Start a background parse with the same parser options used by normal startup: per-chunk loading and lazy rooms, textures, and audio. The background progress callback only updates atomics for logs; it does not draw the loading UI.

When the video ends, join the worker. If parsing succeeded, transfer ownership of the completed `DataWin` to normal startup and skip the later parse. If the worker is still finishing, keep the final video frame/fade or a valid loading screen visible while joining. If it failed, log the reason, destroy partial state, and run the established synchronous parse.

Only parsing moves to the worker in the first version. VM creation, renderer setup, room events, audio initialization, and texture uploads remain in their existing order on the main thread. This keeps the first increment small and gives clean timing evidence before more startup work is moved.

The log records video duration, parse start/end, overlap duration, join wait, peak staging memory, selected affinity, and whether synchronous fallback ran.

### Adaptive frame pacer

Extract deadline calculation into a testable module. Represent the period as an integer quotient plus accumulated remainder so 30, 40, and 60 FPS do not drift from repeated truncation of `1,000,000 / fps`.

Each frame receives one absolute deadline. When early, the pacer performs a coarse delay using an adaptive guard derived from recent sleep overshoot, followed by a bounded short wait. The guard is clamped so it cannot recreate the existing fixed one-millisecond spin. When late, the next deadline starts from the current timestamp; the game never renders multiple catch-up frames.

Reset the pacer after loading, room transitions, video playback, renderer changes, suspend/resume, and any stall exceeding two target frames. Export frame-time histogram data and counts for early, late, reset, and oversleep events in development logs.

At 40 FPS the software pacer remains unsynchronized with the Vita's 60 Hz display. It targets consistent 25 ms simulation frames, accepting that display cadence cannot be perfectly even. Frame pacing improves variance only when CPU and GPU work completes within the frame budget.

### Detached room-payload staging

Introduce a room-prefetch API that opens its own read-only file handle and parses into a detached room payload. It must not use `DataWin.lazyLoadFile` and must not overwrite the current `Room` fields. The main thread validates the generation, then atomically adopts the detached payload before room instance creation and Room Start events.

Known transitions may enqueue prefetch as soon as the destination room is selected. If the destination becomes known only when transition processing begins, the existing fade/loading presentation remains active until the result is ready. Cancellation frees detached allocations on the worker or main thread according to ownership state.

Keep at most two detached payloads: the requested destination and the most recently useful payload when memory permits. Chapter changes and title return clear the cache. The cache is disabled automatically after allocation pressure or when its measured hit rate does not justify its memory cost.

### Texture I/O and decode staging

Split texture preparation into a CPU stage and a graphics commit stage. The worker may read compressed pages, validate headers, and decode formats into an immutable staging buffer. The main thread performs every `glTexImage2D`, `glCompressedTexImage2D`, binding, eviction, and residency update.

Graphics commits use a per-frame byte/time budget and priority queue. Assets required for the immediately visible room take priority; predicted or cached assets yield. A room cannot start drawing with a partially committed mandatory set. Optional assets may continue across later frames only when the game already has a valid fallback.

For Flowey's final battle, use existing room and phase knowledge to enqueue upcoming pages before the corresponding transition. This targets the observed transition spikes. It does not move tower surfaces, shaders, reflections, or GameMaker draw events to the fourth core. Further base-FPS work remains a renderer/surface optimization task and must be judged from GPU/render timings after staging stalls are removed.

Generic GML multithreading is out of scope because the VM and instance state are mutable and not thread-safe. A future host-side particle replacement would require its own immutable input snapshot and command buffer, and should only be considered if profiling proves CPU particle simulation is the bottleneck.

## IOStaging Evaluation

IOStaging is an optional kernel plugin that replaces costly cache-maintenance behavior with DMA-backed staging for sufficiently large reads. Its archived implementation hooks storage modules at firmware-specific offsets and applies system-wide behavior. The project will not package, install, or require it.

Add development-only I/O timing markers around large sequential reads: bytes, source file, duration, throughput, queue wait, and whether the read overlapped rendering. The Vita test matrix runs the identical VPK and save path with IOStaging disabled and enabled, rebooting between configurations. Test Chapter 0 startup, Chapter 3 loading, Chapter 5 room traversal, and Flowey battle entry at least three times per configuration.

IOStaging is documented as optional only if median load time improves without new crashes, data corruption, audio starvation, or worse frame-time tails. The game must not infer plugin presence from a configuration file because that does not prove that the kernel hook loaded successfully.

## CapUnlocker Use

The fourth core is assigned to the single background-work service. Suitable jobs are Chapter 0 parsing, detached room parsing, file reads, texture validation/decode, and bounded audio prebuffer preparation. Audio output, SceAvPlayer ownership, the VM, renderer, surfaces, and graphics API calls stay on their established threads.

CapUnlocker does not provide a second GPU and does not make VitaGL calls thread-safe. It is expected to reduce CPU-side stalls and create main-thread headroom, especially at transitions. The final battle's steady render cost still requires fewer/lighter surface passes, better residency, or the future direct-GXM renderer.

Do not start multiple fourth-core workers for video, textures, and rooms simultaneously. Existing special workers submit compatible work to the central service or retain their established affinity when they have real-time deadlines. The service logs queue latency so contention is visible.

## Error Handling

- Every asynchronous result is tagged with its chapter, room, and generation before adoption.
- A stale or cancelled result is freed without touching live state.
- Worker creation, affinity, allocation, file-open, parse, and decode failures fall back to the existing synchronous behavior.
- Startup never waits indefinitely: worker state is observable and a failed worker triggers cleanup and synchronous parsing.
- Corrupt staged data fails validation before any graphics upload.
- Development logs distinguish I/O time, decode time, main-thread upload time, GPU wait, and total transition time.

## Validation

Host tests cover exact deadline accumulation for 30/40/60 FPS, oversleep adaptation, late-frame reset, generation cancellation, detached-room adoption, stale-result cleanup, staging-budget refusal, and fallback after worker failure.

Desktop local tests load Chapters 0, 3, 4, and 5 through the normal path and exercise repeated room transitions, title return, renderer changes, and battle entry. They verify that asynchronous and synchronous paths produce equivalent room identity, instance counts, and mandatory texture sets.

Vita validation records:

- opening video to interactive Chapter 0 time;
- join wait after the video;
- 30 FPS and 40 FPS frame-time percentiles over fixed routes;
- room-transition median and worst-case duration;
- Flowey battle entry and phase-transition stalls;
- long-session Modern-GL stability and VRAM headroom;
- A/B results with CapUnlocker absent/present;
- separate A/B results with IOStaging disabled/enabled.

No improvement is called complete from average FPS alone. Report median, 95th and 99th percentile frame time, maximum stall, crash/freeze result, and visual correctness.

## Delivery Order

1. Implement and validate the Chapter 0 background parse and adaptive frame pacer. These have narrow ownership boundaries and immediate measurable value.
2. Add detached room-payload prefetch with cancellation and a two-entry maximum cache.
3. Add texture read/decode staging and main-thread budgeted commits, then tune Flowey battle prewarming from logs.
4. Run the IOStaging hardware matrix and document whether it is worth recommending as an optional user configuration.
5. Re-profile the final battle. Address remaining steady GPU cost through surface and renderer changes rather than assigning graphics work to the fourth CPU core.

## Alternatives Rejected or Deferred

- Requiring CapUnlocker or IOStaging is rejected because the base game must start and play without optional kernel plugins.
- Moving arbitrary GML events or live VM objects to another core is rejected because it introduces data races and nondeterministic gameplay.
- Calling VitaGL from a worker is rejected because graphics context ownership and existing renderer state are main-thread concerns.
- A dedicated render thread is deferred to the direct-GXM experiment, where command ownership can be designed explicitly.
- Eagerly loading all Chapter 5 textures is rejected because the observed VRAM headroom is too small and eviction pressure already causes instability.
