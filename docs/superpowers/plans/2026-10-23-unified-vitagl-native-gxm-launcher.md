# Unified VitaGL / Native-GXM Launcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship one DeltaruneVita VPK whose lightweight launcher safely starts either the existing VitaGL port or the experimental Native-GXM port.

**Architecture:** `eboot.bin` owns only renderer selection and process handoff. `vitagl.bin` and `gxm.bin` are independently linked SELF executables that share application data, saves, versioning and progressively more common runtime modules; only one graphics stack exists in a process. The current Native-GXM program is first packaged intact, then its production renderer boundary is moved under Butterscotch without changing the stable VitaGL path.

**Tech Stack:** C99/C11, VitaSDK CMake helpers, SceDisplay CPU framebuffer, SceCtrl, SceAppMgr, Butterscotch `RendererVtable`, VitaGL, SceGxm, Python contract tests, Docker VitaSDK build.

**Spec:** `docs/superpowers/specs/2026-10-23-unified-vitagl-native-gxm-launcher-design.md`

## Global Constraints

- One VPK and existing Title ID `DELT00001`.
- `eboot.bin`, `vitagl.bin`, and `gxm.bin` must be separate SELF processes.
- The launcher must initialize neither VitaGL nor a Native-GXM graphics context.
- VitaGL remains the default for missing, malformed, or unsafe selections.
- Both game processes use `ux0:data/deltarune/deltarunevita/` and the same saves.
- Switching backend always returns through the launcher and restarts the process.
- Native-GXM remains explicitly experimental until renderer parity is validated.
- Existing VitaGL behavior and current VPK assets must remain available.

## Review Focus

- Missing/corrupt preference or missing `gxm.bin`: launcher repairs to VitaGL without looping.
- Missing `vitagl.bin`: launcher remains visible with an installation error rather than starting GXM.
- L+R override and controller debounce: selector remains usable and never launches twice.
- Failed GXM boot marker: launcher explains the previous failure and recommends VitaGL.
- VPK assembly: all three SELF files have matching versions and valid package paths.

---

### Task 1: Backend Preference and Handoff Policy

**Files:**
- Create: `src/vita-common/vita_backend_selection.h`
- Create: `src/vita-common/vita_backend_selection.c`
- Test: `scripts/test-vita-backend-selection.py`

**Interfaces:**
- Produces: `VitaBackend` enum; `VitaBackendSelection_load(...)`; `VitaBackendSelection_save(...)`; `VitaBackendSelection_executable(...)`; `VitaBackendSelection_resolve(...)`; constants for preference, marker, VitaGL and GXM paths.

- [ ] **Step 1: Write the failing backend-selection test**

Assert missing/malformed values select VitaGL, `vitagl` and `gxm` round-trip, missing GXM repairs to VitaGL, missing VitaGL produces a fatal result, and paths are exactly `app0:/vitagl.bin` and `app0:/gxm.bin`.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-vita-backend-selection.py`

Expected: FAIL because the module is absent.

- [ ] **Step 3: Implement the pure policy and filesystem adapter**

Keep parsing and resolution independent of UI/GPU APIs. Use atomic temporary-file replacement for preference updates.

- [ ] **Step 4: Run the test and verify it passes**

Run: `python scripts/test-vita-backend-selection.py`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/vita-common/vita_backend_selection.h src/vita-common/vita_backend_selection.c scripts/test-vita-backend-selection.py
git commit -m "feat: add Vita graphics backend selection policy"
```

### Task 2: Lightweight Native Launcher

**Files:**
- Create: `src/vita-launcher/CMakeLists.txt`
- Create: `src/vita-launcher/main.c`
- Create: `src/vita-launcher/launcher_ui.h`
- Create: `src/vita-launcher/launcher_ui.c`
- Test: `scripts/test-vita-renderer-launcher.py`

**Interfaces:**
- Consumes: Task 1 selection API; `sceAppMgrLoadExec`; SceDisplay and SceCtrl.
- Produces: `eboot.bin`; three-second selector; L+R override; warning/fatal screens; one-shot load-exec state machine.

- [ ] **Step 1: Write the failing launcher state-machine test**

Exercise first launch, countdown, confirmation, L+R hold, missing GXM, missing VitaGL, GXM failed-boot marker, and `sceAppMgrLoadExec` failure. Assert no VitaGL/GXM initialization symbol is linked into launcher sources.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-vita-renderer-launcher.py`

Expected: FAIL because the launcher does not exist.

- [ ] **Step 3: Implement CPU-framebuffer UI and input state machine**

Render the two backend choices and status text with a built-in bitmap font. Debounce controls and allow a failed handoff to return to the menu.

- [ ] **Step 4: Run launcher tests**

Run: `python scripts/test-vita-renderer-launcher.py`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/vita-launcher scripts/test-vita-renderer-launcher.py
git commit -m "feat: add unified Vita renderer launcher"
```

### Task 3: Build VitaGL as a Secondary SELF

**Files:**
- Modify: `src/vita-probe/CMakeLists.txt`
- Modify: `scripts/build-butterscotch-probe.ps1`
- Test: `scripts/test-unified-vita-targets.py`

**Interfaces:**
- Consumes: current playable VitaGL target.
- Produces: `vitagl.bin` with current port behavior and backend/version log header.

- [ ] **Step 1: Write the failing target-layout test**

Assert the production VitaGL runner becomes `vitagl.bin`, keeps all current sources/libraries, and declares `BACKEND=vitagl` without changing renderer selection semantics inside VitaGL.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-unified-vita-targets.py`

Expected: FAIL because the build still emits the runner as `eboot.bin`.

- [ ] **Step 3: Split executable and package targets**

Retain the current runner target and rename only its SELF output. Make the launcher the VPK primary executable.

- [ ] **Step 4: Run existing VitaGL contracts**

Run the unified target test plus renderer, video, startup preload, room memory, Chapter 3 GPU budget and Flowey contracts.

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/vita-probe/CMakeLists.txt scripts/build-butterscotch-probe.ps1 scripts/test-unified-vita-targets.py
git commit -m "build: package VitaGL as selectable Vita backend"
```

### Task 4: Package Native-GXM as `gxm.bin`

**Files:**
- Modify: `experiments/native-gxm/CMakeLists.txt`
- Modify: `src/vita-probe/CMakeLists.txt`
- Modify: `scripts/build-butterscotch-probe.ps1`
- Test: `scripts/test-unified-gxm-target.py`

**Interfaces:**
- Consumes: existing Native-GXM gameplay target and shared version/data root.
- Produces: `gxm.bin` packaged into the unified VPK; `BACKEND=native-gxm` header; launcher-compatible boot marker behavior.

- [ ] **Step 1: Write the failing GXM package contract**

Assert the GXM SELF uses the unified version, Title data root and logs; is not packaged as a second VPK; and remains linked without VitaGL.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-unified-gxm-target.py`

Expected: FAIL because GXM is still an independent package.

- [ ] **Step 3: Add a reusable GXM target and boot markers**

Export the existing gameplay executable as a CMake target consumable by the production package. Write the marker before renderer preflight, clear it after the first successful presented frame, and return to `app0:/eboot.bin` on a handled preflight failure.

- [ ] **Step 4: Run GXM audit and package tests**

Run the unified GXM target test and existing Native-GXM probe contracts.

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add experiments/native-gxm/CMakeLists.txt experiments/native-gxm/main.c src/vita-probe/CMakeLists.txt scripts/build-butterscotch-probe.ps1 scripts/test-unified-gxm-target.py
git commit -m "build: add Native-GXM to unified Vita package"
```

### Task 5: Game Settings Backend Restart

**Files:**
- Modify: `src/vita-probe/source/vita_settings.c`
- Modify: `src/vita-probe/source/vita_settings.h`
- Modify: `src/vita-probe/source/playable_main.c`
- Modify: `experiments/native-gxm/vita_settings_native.c`
- Test: `scripts/test-vita-backend-settings.py`

**Interfaces:**
- Consumes: Task 1 preference API.
- Produces: backend row; restart notice; save-and-return-to-launcher flow in both executables.

- [ ] **Step 1: Write the failing settings test**

Assert both settings implementations expose identical labels/options, preserve VitaGL renderer modes, persist only confirmed changes, flush settings, and return through `app0:/eboot.bin`.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-vita-backend-settings.py`

Expected: FAIL because the backend row is absent.

- [ ] **Step 3: Implement confirmed backend switching**

Use the existing apply/cancel UI. Do not directly load the opposite renderer executable.

- [ ] **Step 4: Run settings and renderer contracts**

Run the new test and existing graphics-description, version and Experimental-GL tests.

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/vita-probe/source/vita_settings.c src/vita-probe/source/vita_settings.h src/vita-probe/source/playable_main.c experiments/native-gxm/vita_settings_native.c scripts/test-vita-backend-settings.py
git commit -m "feat: switch Vita graphics backend from settings"
```

### Task 6: Unified VPK Assembly and Validation

**Files:**
- Modify: `src/vita-probe/CMakeLists.txt`
- Modify: `scripts/build-butterscotch-probe.ps1`
- Create: `scripts/test-unified-vpk.py`
- Create: `docs/v078-unified-renderer-test-protocol.md`

**Interfaces:**
- Consumes: Tasks 2–5 executable targets.
- Produces: versioned unified VPK and host/package validation report.

- [ ] **Step 1: Write the failing package test**

Assert VPK CRC and presence of `eboot.bin`, `vitagl.bin`, `gxm.bin`, LiveArea assets, Chapter 0, opening video and trophy pack. Verify matching embedded build versions.

- [ ] **Step 2: Run the package test and verify it fails**

Run: `python scripts/test-unified-vpk.py <existing-vpk>`

Expected: FAIL because the existing VPK has only `eboot.bin`.

- [ ] **Step 3: Build all targets and assemble one VPK**

Fail the PowerShell build when any SELF is missing. Do not duplicate shared VPK assets.

- [ ] **Step 4: Run the complete host suite and inspect package**

Run all new tests, focused VitaGL contracts, Native-GXM contracts, complete VitaSDK build, VPK CRC and SHA-256.

Expected: all commands PASS and package lists all three SELF files.

- [ ] **Step 5: Document Vita validation routes**

Cover first launch, countdown, L+R, repeated switching, settings restart, failed GXM marker, Chapter 0, save compatibility, Chapter 3 shaders, Chapter 5 shadows and Flowey battle.

- [ ] **Step 6: Commit**

```bash
git add src/vita-probe/CMakeLists.txt scripts/build-butterscotch-probe.ps1 scripts/test-unified-vpk.py docs/v078-unified-renderer-test-protocol.md
git commit -m "build: assemble unified VitaGL and Native-GXM VPK"
```

### Task 7: Start Production GXM Renderer Migration

**Files:**
- Create: `src/butterscotch/src/gxm/gxm_renderer.h`
- Create: `src/butterscotch/src/gxm/gxm_renderer.c`
- Modify: `experiments/native-gxm/bs_renderer_contract.c`
- Test: `scripts/test-gxm-renderer-boundary.py`

**Interfaces:**
- Consumes: Butterscotch `RendererVtable` and proven functions from the experiment.
- Produces: `GXMRenderer_create(DataWin*)`; renderer capability table; experiment adapter without duplicated game bootstrap.

- [ ] **Step 1: Write the failing renderer-boundary test**

Assert the production path implements the complete vtable/capability declaration, contains no launcher or game bootstrap code, and the experiment delegates renderer creation to it.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-gxm-renderer-boundary.py`

Expected: FAIL because the renderer remains under `experiments/`.

- [ ] **Step 3: Move the renderer boundary without changing behavior**

Relocate the proven renderer implementation and keep experimental diagnostics as callers. Backend-specific settings and presentation remain outside the renderer.

- [ ] **Step 4: Run Native-GXM and shared Runner contracts**

Expected: PASS with matching rendered test metrics and no VitaGL symbols in the GXM backend.

- [ ] **Step 5: Commit**

```bash
git add src/butterscotch/src/gxm experiments/native-gxm/bs_renderer_contract.c scripts/test-gxm-renderer-boundary.py
git commit -m "refactor: promote Native-GXM renderer into Butterscotch"
```

