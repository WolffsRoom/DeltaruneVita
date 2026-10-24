# DeltaruneVita Unified VitaGL / Native-GXM Launcher Design

## Intent

DeltaruneVita will ship as one application, one Title ID, and one VPK while
offering two isolated graphics processes: the established VitaGL port and the
experimental Native-GXM renderer. The integration must stop the GXM work from
becoming a second game port with duplicated fixes, assets, saves, configuration,
and release packaging.

## Goals

- Install one VPK under the existing `DELT00001` application.
- Use `eboot.bin` as a lightweight renderer launcher.
- Package the established port as `app0:/vitagl.bin`.
- Package the native renderer as `app0:/gxm.bin`.
- Select the executable before VitaGL or Native-GXM initializes a graphics
  context.
- Share chapter data, language packs, saves, audio, video, controls, trophies,
  border rules, and compatible Butterscotch runtime code.
- Preserve VitaGL as the stable default and label Native-GXM experimental.
- Provide a safe fallback to VitaGL if the selected GXM executable is absent or
  cannot pass its preflight checks.
- Make renderer selection available both from the launcher and from Game
  Settings, with a controlled process restart.

## Non-goals for the first integration

- Mixing VitaGL and direct GXM calls inside the same process or frame.
- Switching graphics backends without restarting the process.
- Claiming feature parity for Native-GXM before all renderer contracts pass.
- Deleting the existing VitaGL renderers.
- Moving VM events, audio output, or GPU work to background threads.
- Rewriting all Native-GXM shaders during launcher integration.

## Anti-goals

- Two separately installed applications with different Title IDs.
- Separate save or configuration directories.
- Copying gameplay fixes manually between two source trees.
- Allowing one backend to initialize before the renderer choice is final.
- Silently leaving the user on a black screen when `gxm.bin` fails.

## Selected architecture

The package contains three SELF executables:

```text
app0:/eboot.bin   -> lightweight launcher
app0:/vitagl.bin  -> current production port
app0:/gxm.bin     -> experimental Native-GXM backend
```

`sceAppMgrLoadExec()` replaces the launcher process with the selected SELF. The
executables are in `app0:`, matching the VitaSDK API requirement. Because the
launcher exits as part of the replacement, its framebuffer, heap, and modules do
not coexist with the selected renderer.

All three targets use one CMake project and one VPK rule. Common game/runtime
sources are compiled through shared target definitions. Backend-specific source
and libraries remain isolated at link time:

- VitaGL target: VitaGL, VitaShaRK, OpenGL renderers and existing compatibility
  paths.
- GXM target: direct GXM renderer, native shader programs and its renderer-only
  support code.
- Launcher target: controller, filesystem, display/framebuffer, and AppMgr only.

## Launcher behavior

On the first launch, or while both shoulder buttons are held during startup, the
launcher displays a native 960x544 selection screen:

- **VitaGL — Stable**
- **Native-GXM — Experimental**

The launcher must not initialize VitaGL or a Native-GXM rendering context. It
uses a CPU-visible framebuffer and a compact built-in bitmap font/UI so renderer
startup remains clean.

The selected value is stored in:

`ux0:data/deltarune/deltarunevita/renderer_backend.ini`

Accepted values are `vitagl` and `gxm`. Missing, malformed, or unknown values
resolve to `vitagl`.

Normal launches show the selection screen briefly and allow changing the saved
choice. A configurable countdown starts the saved backend automatically. The
first implementation uses a three-second countdown, confirms immediately with
Cross, changes selection with the directional controls, and keeps Circle as
cancel/back to the stable VitaGL choice. Holding L+R disables the countdown.

Before calling `sceAppMgrLoadExec`, the launcher verifies that the selected SELF
exists in `app0:`. If `gxm.bin` is missing, it displays a readable warning and
starts `vitagl.bin`. If `vitagl.bin` is missing, it displays a fatal installation
error and remains on screen instead of looping.

## Renderer selection from Game Settings

The graphics settings expose a top-level backend field:

- VitaGL
- Native-GXM (Experimental)

The existing VitaGL renderer field continues to choose Automatic, Legacy,
Modern, or Experimental only while the backend is VitaGL.

Changing the top-level backend writes `renderer_backend.ini`, displays a restart
notice, finishes pending save/config writes, and loads `app0:/eboot.bin`. The
launcher then starts the requested backend. A direct VitaGL-to-GXM jump is not
allowed; returning through the launcher guarantees process teardown.

## Shared runtime structure

The first milestone keeps backend implementations separate but removes product
duplication through build-time shared components:

```text
src/vita-launcher/               launcher and backend preference
src/vita-common/                 shared paths, logs, settings schema, restart
src/vita-probe/                  VitaGL entrypoint and platform presentation
src/butterscotch/src/gxm/        production Native-GXM RendererVtable backend
src/butterscotch/src/            DataWin, VM, Runner and shared systems
```

The existing `experiments/native-gxm` remains a reference until each component
is migrated and covered by a renderer contract. Production code must not include
files directly from an experimental build directory.

The migration order is:

1. Package and launch the current GXM executable from the unified VPK.
2. Move its `RendererVtable` implementation into
   `src/butterscotch/src/gxm/`.
3. Replace its duplicated bootstrap with the common DataWin/VM/Runner bootstrap.
4. Replace duplicated settings, input, borders, audio, video and logging with
   common Vita modules.
5. Remove the standalone GXM VPK target after the unified package reproduces its
   diagnostic and gameplay behavior.

## Data, saves, and logs

Both game executables use the existing external root:

`ux0:data/deltarune/deltarunevita/`

Save compatibility is mandatory. Backend-specific caches are placed in distinct
subdirectories because their binary GPU formats and lifetime rules differ.

Logs identify both the package build and process backend at the first line:

```text
BACKEND=vitagl executable=app0:/vitagl.bin
BACKEND=native-gxm executable=app0:/gxm.bin
```

Native-GXM preflight failures are written to a small launcher-readable status
file before returning to `eboot.bin`. The launcher shows the reason and offers
VitaGL instead of repeatedly reopening the failing backend.

## Failure handling

- Missing or invalid preference: use VitaGL.
- Missing `gxm.bin`: warn, select VitaGL, and repair the preference.
- GXM preflight failure: record the failure, return to launcher, and offer
  VitaGL.
- GXM crash after preflight: the next launcher start detects an uncleared boot
  marker and recommends VitaGL; it never changes the setting without informing
  the user.
- Missing `vitagl.bin`: show an installation error with a VPK reinstall action
  description; do not fall back to GXM automatically.
- Failed `sceAppMgrLoadExec`: display the returned error code and keep the
  launcher responsive.

## Build and package rules

The CMake graph produces three independent SELF files from shared source lists.
The final `vita_create_vpk` uses `eboot.bin` as its primary executable and adds
`vitagl.bin` and `gxm.bin` as ordinary `app0:` files. LiveArea assets, trophy
pack, Chapter 0, opening video and UI assets remain single-copy.

The build script must fail if any of the three SELF files is missing. Package
validation checks their paths and CRC, extracts their build identifiers, and
ensures all three report the same internal version.

## Validation

Host contracts cover:

- valid/default/malformed backend preference parsing;
- first launch and L+R override;
- missing GXM fallback without a relaunch loop;
- missing VitaGL fatal path;
- exact `sceAppMgrLoadExec` destination;
- Game Settings restart flow;
- common version and data/save roots;
- backend-specific library isolation;
- unified VPK contents.

Vita validation covers cold launch, repeated backend switching, suspend/resume,
return from Game Settings, Chapter 0 handoff, videos, audio, trophies, save
compatibility, a Modern-only Chapter 3 shader scene, Chapter 5 town shadows, and
the Flowey final battle. Memory logs must confirm that the launcher process does
not coexist with either renderer.

## Alternatives considered

### One executable containing both backends

This produces fewer SELF files but links incompatible graphics stacks into one
process and makes dependency, startup, and crash isolation harder. It remains a
possible later consolidation after the GXM backend reaches parity.

### Two direct LiveArea actions

Separate LiveArea targets offer a convenient shortcut but do not provide robust
preflight, fallback, first-use guidance or settings-driven restart. They may be
added later as optional shortcuts; they are not the primary selector.

### Two independently installed applications

This gives maximum isolation but duplicates releases and encourages divergence
of saves, settings and gameplay fixes. It conflicts with the main goal and is
rejected.

