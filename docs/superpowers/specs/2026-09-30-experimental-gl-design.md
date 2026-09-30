# Experimental-GL Design

## Objective

Add an opt-in `Experimental-GL` renderer mode that preserves the proven
Legacy-GL texture, surface, batching, submission and lifetime behaviour while
running compatible DELTARUNE shaders through Legacy-GL's programmable shader
bridge. The mode is intended to retain shader-driven presentation without
reintroducing the Modern-GL resource-lifetime failures seen on real Vita.

Success means:

- Round 2 and other rooms that are stable under Legacy-GL retain that
  stability.
- Audited Chapter 3, 4 and 5 2D shaders render in the experimental mode.
- A shader compile, link or compatibility failure falls back for that shader
  without terminating the game or changing renderer ownership.
- `Automatic`, `Legacy-GL` and `Modern-GL` retain their current behaviour.

## Feasibility Finding

The required architecture already exists in partial form. The Legacy renderer
owns a shader-program table, implements GameMaker shader selection/reset and
uniform setters, and compiles an allow-list with a compact Vita vertex stage.
It retains the original game fragment stage. This is the safe foundation for
Experimental-GL.

Experimental-GL will therefore be a policy mode of `GLLegacyRenderer`, not a
third renderer implementation and not runtime switching between Legacy and
Modern. A live backend switch would cross incompatible texture, framebuffer,
surface, batch and shader ownership and is excluded.

## User-Facing Mode

The renderer selector gains a fourth value:

- Automatic
- Legacy-GL
- Modern-GL
- Experimental-GL

The INI value is `renderer=experimental-gl`. Existing INIs remain compatible.
Automatic stays the default. Selecting Experimental-GL requires the existing
restart flow because renderer resources are created at process startup.

The separate Shaders toggle remains authoritative. With Experimental-GL and
Shaders Off, the renderer behaves like Legacy-GL. With Shaders On, the audited
shader bridge is enabled.

## Runtime Architecture

`VitaSettings_forceLegacyRenderer` selects `GLLegacyRenderer_create()` for
Experimental-GL. `g_vitaModernGlActive` stays false so every proven Legacy
cache, upload, surface and frame-submission path remains active. A separate
`g_vitaExperimentalGlActive` flag selects shader policy and diagnostics; code
must not infer experimental mode from the Modern flag.

The shader bridge keeps these rules:

1. Compile only on first selection or during the existing explicit shader
   preparation stage.
2. Use the compact Vita vertex contract for ordinary 2D shaders.
3. Retain the game's fragment program when its varyings, samplers and uniforms
   fit the audited contract.
4. Preserve the existing dedicated adaptations for hue and palette shaders.
5. Fall back to Legacy's textured fixed pipeline when compilation, linking or
   compatibility validation fails.
6. Never instantiate `GLRenderer`, transfer GL objects between renderers, or
   run Modern's texture LRU in this mode.

## Shader Capability Policy

Coverage is capability-based and conservative. The first implementation will
start from the current `legacyCompactShaderSupported` set and audit additional
shaders used by Chapters 3, 4 and 5.

Shaders are eligible when they use the common 2D position, colour and texture
coordinate contract and their sampler/uniform requirements are implemented by
Legacy-GL. Shaders requiring unsupported generated vertex semantics, native
video planes, unimplemented effect layers, unsafe multipass feedback or
Modern-owned surface behaviour remain on fallback.

The initial target includes colour transforms, grayscale, palette operations,
Chapter 3 channel effects, Chapter 4 prophecy/fountain/ripple effects, and
Chapter 5 shadow/logo/force-colour effects already present in the Legacy
allow-list. Additional shaders enter only with a focused regression test and a
successful desktop Legacy shader run.

This mode does not claim complete Modern-GL shader parity. The menu and logs
must label it experimental.

## Failure Handling and Diagnostics

Every requested shader produces one of these outcomes in the probe log:

- `EXPERIMENTAL_SHADER=compiled`
- `EXPERIMENTAL_SHADER=fallback` with compile/link/unsupported reason
- `EXPERIMENTAL_SHADER=active`

Repeated failures are cached and do not trigger recompilation each frame. A
failed shader resets cleanly to the fixed textured path. Room transitions do
not delete experimental shader programs; they follow the existing Legacy
renderer lifetime and are released only when the renderer is destroyed.

The startup log records `renderer=experimental-gl backend=legacy` so Vita
reports cannot be mistaken for Modern-GL runs.

## Code Boundaries

- `vita_settings.h/.c`: enum, INI serialization/parsing, selector label and
  restart handling.
- `playable_main.c`: backend choice, global experimental policy flag and
  startup diagnostics.
- `gl_legacy_renderer.c/.h`: capability policy, compile/fallback diagnostics
  and audited shader coverage.
- Tests under `scripts/` and `src/butterscotch/tests/`: settings contract,
  backend isolation, shader allow-list and local rendering regressions.

No Modern renderer code should be copied into Legacy as part of this work.

## Validation

Automated checks must prove:

- `experimental-gl` round-trips through the INI.
- The selected backend is `GLLegacyRenderer` and
  `g_vitaModernGlActive == false`.
- Experimental shader policy requires both Experimental-GL and Shaders On.
- Unsupported and failed shaders fall back once without retry loops.
- Existing Automatic/Legacy/Modern contracts still pass.
- Desktop local tests compile and exercise the supported Legacy shader set for
  Chapters 3, 4 and 5.
- The Vita Release build produces a valid VPK.

Real-Vita acceptance should cover Chapter 3 Round 2, a Chapter 4 prophecy or
fountain scene, Chapter 5 Town shadows, and a long multi-room session. The log
must show Legacy resource management and experimental shader outcomes, with no
GPU crash. Visual differences from Modern are documented per shader rather
than widening the allow-list speculatively.

## Non-Goals

- Switching renderer backend inside a room or minigame.
- Sharing GL texture or framebuffer objects between Legacy and Modern.
- Enabling every embedded shader without audit.
- Replacing Automatic as the default before real-Vita validation.
- Solving unrelated gameplay or performance issues while adding the mode.
