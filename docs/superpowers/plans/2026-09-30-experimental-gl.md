# Experimental-GL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in Experimental-GL mode that uses Legacy-GL resource management with the audited programmable shader bridge.

**Architecture:** Experimental-GL is a policy on `GLLegacyRenderer`, selected before renderer creation and identified by a dedicated global flag. It never creates or shares resources with `GLRenderer`; compatible shaders use Legacy's compact shader path and every unsupported or failed shader falls back to Legacy's textured path.

**Tech Stack:** C11, Butterscotch renderer vtable, VitaGL/OpenGL shader API, Python contract tests, desktop `local-test`, VitaSDK Docker build.

**Spec:** `docs/superpowers/specs/2026-09-30-experimental-gl-design.md`

## Global Constraints

- `Automatic` remains the default renderer mode.
- `Experimental-GL` must instantiate `GLLegacyRenderer` and keep `g_vitaModernGlActive == false`.
- Experimental shaders require both Experimental-GL and Shaders On.
- No texture, framebuffer, surface, batch or shader object may be transferred between Legacy and Modern.
- Shader failure must use an individual fixed textured fallback without terminating the game or retrying every frame.
- Existing Automatic, Legacy-GL and Modern-GL behaviour must remain unchanged.

## Review Focus

- An old INI without the new value must continue to migrate to Automatic; Task 1 adds this assertion.
- Shaders On under plain Legacy-GL must retain its established behaviour rather than silently becoming Experimental; Task 2 pins the mode flag boundary.
- Experimental-GL with Shaders Off must perform no experimental compilation; Task 2 tests the combined policy predicate.
- An unsupported or failed shader requested every frame must log/fail once and use fallback thereafter; Task 3 covers attempted-state caching.
- Chapter 3 Round 2 must remain on Legacy texture/surface ownership even with experimental shaders enabled; Task 4 checks backend logs and the existing GPU-budget contracts before the Vita test.

---

### Task 1: Renderer Selector and INI Contract

**Files:**
- Create: `scripts/test-experimental-gl-contract.py`
- Modify: `src/vita-probe/source/vita_settings.h`
- Modify: `src/vita-probe/source/vita_settings.c`

**Interfaces:**
- Produces: `VITA_RENDERER_EXPERIMENTAL = 3` and persisted value `renderer=experimental-gl`.
- Produces: selector text `Experimental-GL` and four-value cycling for `pendingRendererMode`.

- [ ] **Step 1: Write the failing settings contract test**

Assert the enum value, INI serialization/parser branch, selector label, four-mode wraparound, Automatic default and legacy migration behaviour.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-experimental-gl-contract.py`
Expected: FAIL because `VITA_RENDERER_EXPERIMENTAL` and `experimental-gl` do not exist.

- [ ] **Step 3: Implement the fourth settings mode**

Add `VITA_RENDERER_EXPERIMENTAL = 3`; update save/load, selector display, left/right cycling and pending-state application. Preserve `renderer_selection_version=1` compatibility and all existing restart semantics.

- [ ] **Step 4: Run the settings test and existing Chapter 3 settings contract**

Run: `python scripts/test-experimental-gl-contract.py && python scripts/test-ch3-gameshow-gpu-budget.py`
Expected: both PASS.

- [ ] **Step 5: Commit**

Commit only the settings files and new test with message `feat: add Experimental-GL renderer option`.

### Task 2: Legacy Backend Isolation and Policy Flag

**Files:**
- Modify: `scripts/test-experimental-gl-contract.py`
- Modify: `src/vita-probe/source/playable_main.c`
- Modify: `src/vita-probe/source/vita_settings.c`
- Modify: `src/butterscotch/src/runner.c`

**Interfaces:**
- Consumes: `VITA_RENDERER_EXPERIMENTAL` from Task 1.
- Produces: global `bool g_vitaExperimentalGlActive`.
- Produces: startup log `RENDERER=experimental-gl backend=legacy shaders=<0|1>`.

- [ ] **Step 1: Extend the failing contract test for backend isolation**

Assert Experimental selects the Legacy factory, leaves `g_vitaModernGlActive` false, sets the dedicated flag, and gates experimental shader work on `g_vitaShadersEnabled`.

- [ ] **Step 2: Run the focused test and verify it fails**

Run: `python scripts/test-experimental-gl-contract.py`
Expected: FAIL because the experimental runtime flag and log do not exist.

- [ ] **Step 3: Implement startup policy**

Define the global in `playable_main.c`, expose it where renderer/runner policy needs it, route Experimental through `GLLegacyRenderer_create()`, and emit the unambiguous startup log. Keep `g_vitaModernGlActive` false.

- [ ] **Step 4: Run focused and renderer regression contracts**

Run: `python scripts/test-experimental-gl-contract.py && python scripts/test-modern-room-jump-memory-contracts.py && python scripts/test-ch3-gameshow-gpu-budget.py`
Expected: all PASS.

- [ ] **Step 5: Commit**

Commit the runtime-policy changes and updated test with message `feat: isolate Experimental-GL on Legacy backend`.

### Task 3: Shader Capability and Safe Fallback

**Files:**
- Modify: `scripts/test-experimental-gl-contract.py`
- Modify: `src/butterscotch/src/gl_legacy/gl_legacy_renderer.c`
- Modify: `src/butterscotch/src/gl_legacy/gl_legacy_renderer.h`

**Interfaces:**
- Consumes: `g_vitaExperimentalGlActive` and `g_vitaShadersEnabled` from Task 2.
- Produces: `GLLegacyRenderer_experimentalShadersEnabled(const GLLegacyRenderer*) -> bool` or an equivalent single policy boundary used by compile/select/uniform paths.
- Produces: one-shot `EXPERIMENTAL_SHADER=compiled|fallback|active` diagnostics with shader name/index and fallback reason.

- [ ] **Step 1: Extend the failing contract test for shader policy**

Assert a single combined policy predicate, the current audited allow-list, one-shot attempted-state fallback, and the three diagnostic outcomes. Assert `video_yuv` and unsupported effect/multipass shaders remain excluded.

- [ ] **Step 2: Run the test and verify it fails**

Run: `python scripts/test-experimental-gl-contract.py`
Expected: FAIL because experimental diagnostics and the combined policy boundary do not exist.

- [ ] **Step 3: Gate the existing compact shader bridge**

Use the existing `legacyCompactShaderSupported` table and compact vertex adaptations. Experimental mode with Shaders On may compile/select them; unsupported or failed programs set `shaderAttempted[index]`, reset to the textured path and log one fallback reason. Do not copy Modern renderer code or broaden the allow-list without an existing local regression target.

- [ ] **Step 4: Add compile/active diagnostics without per-frame spam**

Log compile/fallback once per shader attempt and active only on a shader-state transition. Include shader index and resource name.

- [ ] **Step 5: Run the focused contracts and local Legacy shader regression**

Run: `python scripts/test-experimental-gl-contract.py`
Run the existing `local-test` shader audit for Chapters 3, 4 and 5 with the Legacy renderer and shader bridge enabled.
Expected: contract PASS; no compile/link error for the audited set; unsupported shaders use fallback.

- [ ] **Step 6: Commit**

Commit renderer and test changes with message `feat: run audited shaders in Experimental-GL`.

### Task 4: Full Regression, Vita Build, and Test Artifact

**Files:**
- Modify only if a regression proves a defect in Tasks 1-3.
- Output: versioned VPK under `artifacts/`.

**Interfaces:**
- Consumes: complete Experimental-GL mode from Tasks 1-3.
- Produces: installable Vita test build and verification evidence.

- [ ] **Step 1: Run all targeted Python contracts**

Run the experimental contract plus Chapter 3 GPU budget, startup guards, Modern room-jump memory, BC3, Chapter 5 battle and Chapter 5 cliff contracts.
Expected: all PASS.

- [ ] **Step 2: Run desktop local tests**

Exercise Chapters 3, 4 and 5 under Legacy and Experimental shader policy. Confirm Automatic/Legacy/Modern startup still works and collect shader compile/fallback logs.
Expected: no crash; audited shaders compile; fallback is bounded and repeat-free.

- [ ] **Step 3: Build the Vita Release VPK**

Run the established `scripts/build-butterscotch-probe.ps1` Release workflow with the next test build suffix.
Expected: VitaSDK build succeeds and produces a non-empty VPK.

- [ ] **Step 4: Inspect the artifact**

Verify VPK entries, size, SHA-256 and embedded version label. Record the exact path for the user.

- [ ] **Step 5: Review the complete diff**

Confirm the final diff contains no Modern resource ownership in the experimental branch and does not include unrelated pre-existing workspace changes.

- [ ] **Step 6: Commit final test/build metadata if tracked**

Commit only necessary test or version metadata with message `test: validate Experimental-GL Vita build`.

- [ ] **Step 7: Real-Vita acceptance**

Install the VPK and test Chapter 3 Round 2, one Chapter 4 prophecy/fountain scene, Chapter 5 Town shadows, and a long multi-room session. Confirm logs identify `backend=legacy`, shaders show bounded outcomes, and no GPU crash occurs.
