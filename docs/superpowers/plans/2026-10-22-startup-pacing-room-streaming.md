# Vita Startup, Frame Pacing, and Room Streaming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Overlap Chapter 0 parsing with the opening video, stabilize the 30/40 FPS limiter, and move safe room and texture preparation off the Vita render thread.

**Architecture:** A single bounded background-work service owns CapUnlocker-aware CPU jobs and returns immutable or detached results to the main thread. Startup parsing, detached room parsing, and texture I/O/decode use that service; all VM events and VitaGL/GXM work remain on the main thread. An adaptive absolute-deadline pacer replaces the fixed one-millisecond spin, while development telemetry supports Vita A/B testing of optional IOStaging.

**Tech Stack:** C99/C11, Butterscotch data.win parser and runner, VitaSDK thread/time APIs, VitaGL, Python contract tests, desktop `local-test`, Docker VitaSDK build.

**Spec:** `docs/superpowers/specs/2026-10-22-startup-pacing-room-streaming-design.md`

## Global Constraints

- CapUnlocker and IOStaging remain optional; the same VPK must work without either plugin.
- No VitaGL, GXM, GameMaker event, live VM mutation, or audio-output call may execute in a background job.
- Background staging memory is capped at 8 MiB, with one memory-heavy job executing at a time.
- Every asynchronous result carries a generation and is discarded if its chapter or room is stale.
- Graphics uploads and room instance creation remain on the main thread.
- 40 FPS uses a 25 ms software cadence and is not presented as synchronized to the Vita's 60 Hz display.
- Performance claims require median, 95th percentile, 99th percentile, maximum stall, stability, and visual-correctness results.

## Review Focus

- CapUnlocker absent or thread creation rejected: tests must prove the normal-core fallback performs the same job exactly once.
- Opening video missing or falling back to PNG frames: tests must prove the Chapter 0 parse is joined/adopted without a duplicate parse.
- Chapter/title changes while room work is pending: tests must prove stale generations cannot mutate the active room.
- A decoded texture larger than the staging budget: tests must prove it stays on the synchronous path without leaking the partial buffer.
- Suspend, loading stall, or renderer change during frame pacing: tests must prove the deadline resets without catch-up frames.

---

### Task 1: Central Vita Background Work Service

**Files:**
- Create: `src/vita-probe/source/vita_background_work.h`
- Create: `src/vita-probe/source/vita_background_work.c`
- Modify: `src/vita-probe/CMakeLists.txt`
- Modify: `src/vita-probe/source/vita_thread_policy.h`
- Test: `scripts/test-vita-background-work.py`

**Interfaces:**
- Consumes: `VitaThread_createBackground(...)` from `vita_thread_policy.h`.
- Produces: opaque `VitaBackgroundWork`; `VitaBackgroundWork_create(size_t budgetBytes)`; `VitaBackgroundWork_submit(VitaBackgroundWork *, VitaBackgroundJob *)`; `VitaBackgroundWork_poll(...)`; `VitaBackgroundWork_cancelGeneration(...)`; `VitaBackgroundWork_destroy(...)`; immutable job-result ownership rules; an 8 MiB default budget.

- [ ] **Step 1: Write the failing service contract test**

Assert that the new header exposes creation, submission, polling, generation cancellation, destruction, and budget constants; the source creates exactly one CapUnlocker-aware worker; and no graphics/VM symbols occur in the worker implementation.

- [ ] **Step 2: Run the contract test and verify it fails**

Run: `python scripts/test-vita-background-work.py`

Expected: FAIL because the service files and interfaces do not exist.

- [ ] **Step 3: Implement the bounded queue and ownership model**

Use one mutex/condition-backed queue and one worker. Define explicit job states (`QUEUED`, `RUNNING`, `READY`, `FAILED`, `CANCELLED`) and require each result destructor to work without a graphics context. Refuse jobs whose reserved staging bytes would exceed 8 MiB.

- [ ] **Step 4: Add a host simulation to the test**

Compile the queue state/generation logic with mocked thread primitives. Assert FIFO execution, one heavy job at a time, fallback after worker-create failure, stale-generation cleanup, and budget refusal.

- [ ] **Step 5: Run service and existing CapUnlocker tests**

Run: `python scripts/test-vita-background-work.py`

Run: `python scripts/test-capunlocker-contract.py`

Expected: both PASS.

- [ ] **Step 6: Commit the service**

```bash
git add src/vita-probe/source/vita_background_work.h src/vita-probe/source/vita_background_work.c src/vita-probe/source/vita_thread_policy.h src/vita-probe/CMakeLists.txt scripts/test-vita-background-work.py
git commit -m "feat: add bounded Vita background work service"
```

### Task 2: Adaptive Absolute-Deadline Frame Pacer

**Files:**
- Create: `src/vita-probe/source/vita_frame_pacer.h`
- Create: `src/vita-probe/source/vita_frame_pacer.c`
- Modify: `src/vita-probe/source/playable_main.c:4199-4205`
- Modify: `src/vita-probe/source/playable_main.c:5309-5341`
- Modify: `src/vita-probe/CMakeLists.txt`
- Test: `scripts/test-vita-frame-pacer.py`

**Interfaces:**
- Consumes: target FPS from Vita settings and `sceKernelGetProcessTimeWide()`/`sceKernelDelayThread()` at the platform boundary.
- Produces: `VitaFramePacer_init(VitaFramePacer *, uint32_t fps, uint64_t nowUs)`; `VitaFramePacer_setRate(...)`; `VitaFramePacer_plan(...)`; `VitaFramePacer_noteSleep(...)`; `VitaFramePacer_finish(...)`; `VitaFramePacer_reset(...)`; counters for late, oversleep, and reset events.

- [ ] **Step 1: Write deterministic failing timing tests**

Test exact accumulated deadlines across 300 frames at 30, 40, and 60 FPS; adaptive guard clamping; a late frame that resets from `now`; a stall over two periods; rate changes; and no catch-up deadline after suspend/loading.

- [ ] **Step 2: Run the pacer test and verify it fails**

Run: `python scripts/test-vita-frame-pacer.py`

Expected: FAIL because the pacer module does not exist.

- [ ] **Step 3: Implement pure timing calculations**

Store quotient and remainder for `1,000,000 / fps`, carry remainder between frames, derive the coarse-sleep guard from measured overshoot, and clamp the final busy wait to a short bounded tail.

- [ ] **Step 4: Replace the inline limiter and add reset points**

Wire the module into `playable_main.c`. Reset after startup video, loading completion, room transition, renderer/settings changes, suspend/resume, and stalls exceeding two target periods. Keep uncapped mode free of delay.

- [ ] **Step 5: Run pacing and settings regressions**

Run: `python scripts/test-vita-frame-pacer.py`

Run: `python scripts/test-experimental-gl-contract.py`

Expected: PASS with exact 25,000 us cadence at 40 FPS and no renderer-selection regression.

- [ ] **Step 6: Commit the pacer**

```bash
git add src/vita-probe/source/vita_frame_pacer.h src/vita-probe/source/vita_frame_pacer.c src/vita-probe/source/playable_main.c src/vita-probe/CMakeLists.txt scripts/test-vita-frame-pacer.py
git commit -m "perf: add adaptive Vita frame pacing"
```

### Task 3: Parse Chapter 0 During the Opening Video

**Files:**
- Create: `src/vita-probe/source/vita_startup_preload.h`
- Create: `src/vita-probe/source/vita_startup_preload.c`
- Modify: `src/vita-probe/source/playable_main.c:3550-3840`
- Modify: `src/vita-probe/CMakeLists.txt`
- Test: `scripts/test-chapter0-startup-preload.py`

**Interfaces:**
- Consumes: Task 1 `VitaBackgroundWork`, resolved Chapter 0/mod path, and the existing `DataWinParserOptions` construction.
- Produces: `VitaStartupPreload_begin(...)`; `VitaStartupPreload_join(...) -> DataWin *`; `VitaStartupPreload_cancel(...)`; timing record containing parse, overlap, join, affinity, and fallback fields.

- [ ] **Step 1: Write the failing startup state-machine test**

Mock video success, MP4 failure with PNG fallback, parse success before video end, parse success after video end, worker failure, invalid data, and mod-path startup. Assert exactly one accepted `DataWin_parse` result and synchronous fallback only after failure.

- [ ] **Step 2: Run the startup test and verify it fails**

Run: `python scripts/test-chapter0-startup-preload.py`

Expected: FAIL because startup preload is absent.

- [ ] **Step 3: Resolve the final Chapter 0 path before video playback**

Move only path/options preparation ahead of `first_load_video_play()`. Preserve existing chapter selection, mod rules, first-load fallback, and dependency validation order.

- [ ] **Step 4: Implement background parse and adoption**

The worker calls `DataWin_parse()` with a progress callback that writes atomics only. After video/fallback completion, join and adopt a successful result; otherwise clean partial state and execute the existing synchronous parse. Keep the final video frame or loading presentation valid while joining.

- [ ] **Step 5: Add startup telemetry and run tests**

Log `STARTUP_PRELOAD` fields for parse duration, video overlap, join wait, selected affinity, peak reservation, and fallback reason.

Run: `python scripts/test-chapter0-startup-preload.py`

Run: `python scripts/test-vita-video-state.py`

Expected: PASS; no worker path calls a loading-screen draw function.

- [ ] **Step 6: Commit startup overlap**

```bash
git add src/vita-probe/source/vita_startup_preload.h src/vita-probe/source/vita_startup_preload.c src/vita-probe/source/playable_main.c src/vita-probe/CMakeLists.txt scripts/test-chapter0-startup-preload.py
git commit -m "perf: preload Chapter 0 during intro video"
```

### Task 4: Detached Lazy Room Payload API

**Files:**
- Modify: `src/butterscotch/src/data_win.h:920-960`
- Modify: `src/butterscotch/src/data_win.c:1573-1620`
- Modify: `src/butterscotch/src/data_win.c:3174-3210`
- Test: `scripts/test-room-payload-staging.py`

**Interfaces:**
- Consumes: `DataWin.lazyLoadFilePath`, room offsets, and the existing room-payload parser.
- Produces: opaque `DataWinRoomPayload`; `DataWin_stageRoomPayload(const DataWin *, int32_t roomIndex, DataWinRoomPayload **)`; `DataWin_adoptRoomPayload(DataWin *, int32_t, DataWinRoomPayload *)`; `DataWin_freeStagedRoomPayload(...)`.

- [ ] **Step 1: Write a failing detached-payload test**

Construct a minimal data.win fixture with two lazy rooms. Assert staging opens a separate file handle, leaves both live `Room` objects unchanged, adopts into the requested room, rejects an index mismatch, and frees a staged payload after cancellation.

- [ ] **Step 2: Run the payload test and verify it fails**

Run: `python scripts/test-room-payload-staging.py`

Expected: FAIL because detached staging APIs do not exist.

- [ ] **Step 3: Separate parsing from live-room mutation**

Reuse the existing payload readers against an owned detached `Room` container. Read through a worker-owned `FILE *` opened from `lazyLoadFilePath`; never seek `lazyLoadFile` from the staged path.

- [ ] **Step 4: Implement validated main-thread adoption**

Require matching room index and an unloaded destination. Transfer owned arrays exactly once, zero the detached owner, and keep `DataWin_loadRoomPayload()` as the synchronous compatibility wrapper.

- [ ] **Step 5: Run parser regressions**

Run: `python scripts/test-room-payload-staging.py`

Run: `python scripts/test-modern-room-jump-memory-contracts.py`

Expected: PASS with synchronous callers unchanged.

- [ ] **Step 6: Commit detached payload support**

```bash
git add src/butterscotch/src/data_win.h src/butterscotch/src/data_win.c scripts/test-room-payload-staging.py
git commit -m "perf: stage lazy room payloads off-thread"
```

### Task 5: Runner Room Prefetch, Cancellation, and Cache

**Files:**
- Modify: `src/butterscotch/src/runner.h:680-725`
- Modify: `src/butterscotch/src/runner.c:4169-4550`
- Modify: `src/vita-probe/source/playable_main.c:5100-5160`
- Test: `scripts/test-vita-room-prefetch.py`

**Interfaces:**
- Consumes: Task 1 work service and Task 4 detached room payload API.
- Produces: `Runner_requestRoomPrefetch(Runner *, int32_t roomIndex)`; `Runner_pollRoomPrefetch(Runner *)`; `Runner_cancelRoomPrefetch(Runner *)`; maximum two staged entries and transition timing fields.

- [ ] **Step 1: Write failing transition and cancellation tests**

Cover a prefetched hit, miss with synchronous fallback, destination replaced before completion, chapter/title cancellation, allocation failure, two-entry eviction, and repeated forward/back room transitions. Assert Room End/Start order is unchanged.

- [ ] **Step 2: Run the runner test and verify it fails**

Run: `python scripts/test-vita-room-prefetch.py`

Expected: FAIL because the runner has no prefetch state.

- [ ] **Step 3: Add prefetch state without changing room events**

Queue staging when a pending destination becomes known, poll/adopt only on the main thread, and keep the existing transition/loading presentation active while mandatory payload data is unavailable. Clear both cache entries on chapter/title reset or pressure.

- [ ] **Step 4: Add transition telemetry**

Log queue wait, file read, adoption, instance creation, texture preparation, event time, total transition, cache hit/miss, and cancellation generation.

- [ ] **Step 5: Run room and gameplay regressions**

Run: `python scripts/test-vita-room-prefetch.py`

Run: `python scripts/test-modern-room-jump-memory-contracts.py`

Run: `python scripts/test-ch3-gameshow-gpu-budget.py`

Expected: PASS without changed event order or increased retained room count.

- [ ] **Step 6: Commit room prefetch**

```bash
git add src/butterscotch/src/runner.h src/butterscotch/src/runner.c src/vita-probe/source/playable_main.c scripts/test-vita-room-prefetch.py
git commit -m "perf: prefetch Vita room payloads"
```

### Task 6: Texture Read/Decode Staging and Flowey Priorities

**Files:**
- Modify: `src/butterscotch/src/data_win.h:949-960`
- Modify: `src/butterscotch/src/data_win.c`
- Modify: `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`
- Modify: `src/butterscotch/src/gl/gl_renderer.c`
- Modify: `src/butterscotch/src/runner.c:4400-4550`
- Test: `scripts/test-vita-texture-staging.py`
- Test: `scripts/test-finale-render.py`

**Interfaces:**
- Consumes: Task 1 work service, lazy TXTR metadata, renderer texture residency API, and active-room/Flowey phase information.
- Produces: opaque CPU-only `DataWinStagedTexture`; `DataWin_stageTexture(...)`; `DataWin_freeStagedTexture(...)`; renderer-side `commitStagedTexture(...)`; priority classes `MANDATORY`, `NEXT_PHASE`, and `SPECULATIVE`.

- [ ] **Step 1: Write failing CPU-stage and upload-boundary tests**

Assert file read/header validation/decode occur without GL symbols; oversized pages are refused by the 8 MiB budget; corrupt buffers never reach commit; cancellation frees buffers; and the main-thread commit receives the original texture ID, format, dimensions, and immutable bytes.

- [ ] **Step 2: Run the texture test and verify it fails**

Run: `python scripts/test-vita-texture-staging.py`

Expected: FAIL because staged texture interfaces are absent.

- [ ] **Step 3: Split CPU preparation from renderer upload**

Preserve compressed BC3/PVR data when directly uploadable; decode only formats required by the selected renderer. Keep binding, eviction, residency accounting, and all GL calls inside the renderer commit.

- [ ] **Step 4: Add budgeted priority commits**

Commit mandatory room assets before visible drawing. Process next-phase and speculative assets within a measured per-frame byte/time allowance, yielding immediately when the frame is late or VRAM policy requests eviction.

- [ ] **Step 5: Add Flowey phase prewarming without eager Chapter 5 loading**

Derive the known page sets from existing final-battle instrumentation and queue only the next phase. Preserve structural tower surfaces and the lightweight tower shader; do not move surfaces or draw events to the worker.

- [ ] **Step 6: Run renderer/finale regressions**

Run: `python scripts/test-vita-texture-staging.py`

Run: `python scripts/test-finale-render.py`

Run: `python scripts/test-modern-room-jump-memory-contracts.py`

Expected: PASS with identical mandatory texture IDs and no increase in the protected resident set.

- [ ] **Step 7: Commit texture staging**

```bash
git add src/butterscotch/src/data_win.h src/butterscotch/src/data_win.c src/butterscotch/src/gl_legacy/gl_legacy_renderer.c src/butterscotch/src/gl/gl_renderer.c src/butterscotch/src/runner.c scripts/test-vita-texture-staging.py scripts/test-finale-render.py
git commit -m "perf: stage Vita textures and Flowey phases"
```

### Task 7: I/O Telemetry and Optional IOStaging Test Protocol

**Files:**
- Create: `src/vita-probe/source/vita_io_profile.h`
- Create: `src/vita-probe/source/vita_io_profile.c`
- Modify: `src/vita-probe/CMakeLists.txt`
- Modify: `src/vita-probe/source/vita_startup_preload.c`
- Modify: `src/butterscotch/src/data_win.c`
- Create: `docs/iostaging-v078-test-protocol.md`
- Test: `scripts/test-vita-io-profile.py`

**Interfaces:**
- Consumes: timestamps and byte counts from startup, room, and texture reads.
- Produces: `VitaIoProfile_begin(...)`; `VitaIoProfile_end(...)`; `IO_PROFILE` log records; a manual disabled/enabled IOStaging matrix that never attempts plugin detection or installation.

- [ ] **Step 1: Write the failing telemetry format test**

Assert records contain operation, bytes, path class, queue wait, read time, decode time, overlap flag, success/failure, and throughput; assert release builds can compile telemetry out or rate-limit it.

- [ ] **Step 2: Run the telemetry test and verify it fails**

Run: `python scripts/test-vita-io-profile.py`

Expected: FAIL because telemetry files do not exist.

- [ ] **Step 3: Implement instrumentation without plugin probing**

Instrument only large reads and asynchronous operations. Do not check an IOStaging configuration file as proof of a loaded kernel plugin.

- [ ] **Step 4: Document the Vita A/B protocol**

Use the identical VPK/save and three cold runs per condition, rebooting after plugin changes. Record Chapter 0 startup, Chapter 3 load, Chapter 5 traversal, Flowey entry, audio continuity, corruption, crash/freeze status, and frame-time tails.

- [ ] **Step 5: Run telemetry tests**

Run: `python scripts/test-vita-io-profile.py`

Expected: PASS with stable parseable log fields.

- [ ] **Step 6: Commit telemetry and protocol**

```bash
git add src/vita-probe/source/vita_io_profile.h src/vita-probe/source/vita_io_profile.c src/vita-probe/source/vita_startup_preload.c src/butterscotch/src/data_win.c src/vita-probe/CMakeLists.txt docs/iostaging-v078-test-protocol.md scripts/test-vita-io-profile.py
git commit -m "perf: add Vita IO staging telemetry"
```

### Task 8: Integrated Local Validation and Vita Build

**Files:**
- Modify: `docs/changelog.md`
- Create: `docs/v078-async-loading-validation.md`
- Generated: `artifacts/current/Deltarune-v0.78-<build>.vpk`

**Interfaces:**
- Consumes: all earlier tasks.
- Produces: consolidated test evidence, exact VPK build tag, and a Vita checklist separating CPU staging gains from GPU rendering limits.

- [ ] **Step 1: Run the focused host suite**

Run all new tests plus:

`python scripts/test-capunlocker-contract.py`

`python scripts/test-vita-video-state.py`

`python scripts/test-experimental-gl-contract.py`

`python scripts/test-modern-room-jump-memory-contracts.py`

`python scripts/test-ch3-gameshow-gpu-budget.py`

`python scripts/test-finale-render.py`

Expected: every command PASS.

- [ ] **Step 2: Run desktop local tests**

Exercise Chapters 0, 3, 4, and 5 in Legacy, Modern, Automatic, and Experimental modes where supported. Cover opening handoff, repeated room changes, title return, renderer changes, battle entry, and Flowey transitions. Record any pixel or state difference from the synchronous baseline.

- [ ] **Step 3: Build the Vita VPK**

Run: `powershell -ExecutionPolicy Bypass -File scripts/build-butterscotch-probe.ps1`

Expected: successful VitaSDK build and a versioned VPK under `artifacts/current`.

- [ ] **Step 4: Inspect the package and write validation notes**

Verify `eboot.bin`, LiveArea assets, opening video, and expected configuration files. Record build tag, SHA-256, test commands, limitations, and exact Vita routes in `docs/v078-async-loading-validation.md`.

- [ ] **Step 5: Run the hardware checklist**

Compare CapUnlocker absent/present and IOStaging disabled/enabled independently. Report startup overlap/join time, room median/worst transition, 30/40 FPS percentiles, Flowey entry/phase stalls, VRAM headroom, audio, visual correctness, and crash/freeze result.

- [ ] **Step 6: Commit validation documentation**

```bash
git add docs/changelog.md docs/v078-async-loading-validation.md
git commit -m "docs: record Vita async loading validation"
```
