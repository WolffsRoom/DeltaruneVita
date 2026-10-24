# NXRUNE Dynamic Borders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Match NXRUNE dynamic border selection and visibility for Chapters 1-5 with external Vita border assets.

**Architecture:** Move deterministic selection into a pure `vita_border_rules` module. Sample required GameMaker state from the existing Runner/VM and feed it to the Vita border renderer, which retains responsibility for texture lifetime, crossfade and overlay rendering.

**Tech Stack:** C11, Butterscotch GameMaker runtime, VitaGL, Python regression tests, UndertaleModTool reference dumps.

**Spec:** `docs/superpowers/specs/2026-09-16-nxrune-dynamic-borders-design.md`

## Global Constraints

- NXRUNE patched `data.win` files are reference-only and are never shipped.
- Border images remain external under `ux0:data/deltarune/deltarunevita/borders/`.
- Existing Simple mode, Better Borders, config overrides and memory-pressure behavior must remain functional.
- Unknown state resolves conservatively; no new substring guessing for Chapters 3-5.

---

### Task 1: Consolidate NXRUNE reference data

**Files:**
- Create: `artifacts/nxrune-border-analysis/border-behavior.csv`
- Create: `artifacts/nxrune-border-analysis/border-behavior.md`

- [ ] Generate object/event/room associations from decompiled GML and room-object TSV files.
- [ ] Record exact Chapter 1-5 base room groups and all direct `set_border`, `show_border`, `hide_border` calls.
- [ ] Verify Chapter 5 numeric room arrays against `chapter5.win.rooms.csv`.

### Task 2: Add pure border resolver

**Files:**
- Create: `src/vita-probe/source/vita_border_rules.h`
- Create: `src/vita-probe/source/vita_border_rules.c`
- Modify: `src/vita-probe/CMakeLists.txt`
- Test: `tests/test_vita_border_rules.py`

- [ ] Write failing mapping tests for representative rooms from Chapters 1-5.
- [ ] Implement border IDs and exact room/state resolver.
- [ ] Run mapping tests and fix only rules supported by NXRUNE reference data.

### Task 3: Feed GameMaker state to borders

**Files:**
- Modify: `src/vita-probe/source/vita_borders.h`
- Modify: `src/vita-probe/source/vita_borders.c`
- Modify: `src/vita-probe/source/playable_main.c`

- [ ] Add a compact `VitaBorderGameState` update API.
- [ ] Sample `plot`, `darkzone`, `flag[1324]` and required scene state from `runner->vmContext`.
- [ ] Re-resolve a border when state changes inside the same room.

### Task 4: Reproduce dynamic border overlay

**Files:**
- Modify: `src/vita-probe/source/vita_borders.c`
- Test: `tests/test_vita_border_overlay.py`

- [ ] Add overlay alpha/target/speed/color state matching NXRUNE semantics.
- [ ] Draw overlay after external border art and before the application surface composite.
- [ ] Preserve border texture residency while hidden.

### Task 5: Chapter 3 and Chapter 5 scene parity

**Files:**
- Modify: `src/vita-probe/source/vita_border_rules.c`
- Modify: `src/butterscotch/src/runner.c` only where a verified event signal cannot be sampled from `playable_main.c`.
- Test: `tests/test_vita_border_rules.py`

- [ ] Replace Chapter 3 sprite/sound guessing with state/event rules recovered from NXRUNE.
- [ ] Implement Chapter 5 morning/sunset/night and Garden/Cliff/Castle Left/Right/Gold/Top/Cafe/Pink mapping.
- [ ] Implement verified same-room Castle Top/Gold and Pink alternate transitions.

### Task 6: Chapter 1/2/4 compatibility rules

**Files:**
- Modify: `src/vita-probe/source/vita_border_rules.c`
- Test: `tests/test_vita_border_rules.py`

- [ ] Port Chapter 1 light/dark base behavior and visibility exceptions.
- [ ] Port Chapter 2 Cyber/City/Mansion range mapping and verified visibility states.
- [ ] Port Chapter 4 Church A/B/C, Castle Town and Titan mappings plus verified hide/show cases.

### Task 7: Verification

**Files:**
- Test: `tests/test_vita_border_rules.py`
- Test: `tests/test_vita_border_overlay.py`

- [ ] Run Python regression suite.
- [ ] Build the Vita probe target.
- [ ] Review diff limited to border subsystem integration.
- [ ] Do not package a test VPK until the build and regressions pass.
