# DELTARUNE chapters 1-5 — initial Vita validation

Build ID: `DLTVITA-0001`

Status: `[IMPLEMENTADO, AGUARDANDO TESTE]`

This revision adds diagnostics only. It does not contain the APK, `libyoyo.so`,
WAD files, music, videos, or any other proprietary game data.

## Exact target

- APK filename used during analysis: `deltarune-chapter-1-5-android-port-by-hadrian767.apk`
- APK size: `746349483` bytes
- APK SHA-256: `C25CCCCB42CD7C62FC41480B188F72367F60F123FEC395A762E072CC4DB7AC48`
- Package: `com.hadrian.deltarune`
- Runner: `lib/armeabi-v7a/libyoyo.so`
- Runner format: ELF32 ARM EABI5 soft-float

## What revision 0001 changes

- creates a fresh `ux0:data/gms/deltarune/port.log` for each launch;
- records the Build ID and exact target APK identity;
- records every GameMaker extension call reaching `CallExtensionFunction`;
- preserves the existing DELTARUNE keymap and `glReadPixels` workaround.

## Installation for the first hardware test

1. Build YoYo Loader with `LOADER=ON` and video support enabled.
2. Install the resulting YoYo Loader VPK and its documented prerequisites.
3. Create `ux0:data/gms/deltarune/`.
4. Copy the legally obtained target APK as `ux0:data/gms/deltarune/game.apk`.
5. Copy this directory's `yyl.cfg` to `ux0:data/gms/deltarune/yyl.cfg`.
6. Start the game once and do not run APK optimization or asset externalization.
7. Retrieve `ux0:data/gms/deltarune/port.log` and any matching `psp2core` dump.

## Expected markers

The log must start with:

```text
BUILD_ID=DLTVITA-0001
TARGET_APK_SHA256=C25CCCCB42CD7C62FC41480B188F72367F60F123FEC395A762E072CC4DB7AC48
TARGET_APK_SIZE=746349483
GAME_NAME=deltarune
```

Later expected markers include:

```text
Detected DELTARUNE as Game ID
Enabling Deltarune specific gamehack!
Startup ended
```

## Decision criteria

- `[CONFIRMADO NO HARDWARE]`: the title reaches gameplay and accepts input.
- `[CONFIRMADO POR LOG OU CORE]`: the last marker identifies the failing phase or
  a matching core dump identifies its PC/LR.
- If `Startup ended` is absent, inspect the final JNI, asset, or extension marker.
- If an undefined `WADLoader` call is logged, implement only that exact method.
- If startup completes but a chapter WAD cannot be opened, instrument the exact
  requested path before changing asset behavior.
- Do not enable memory squeezing, externalization, shader dumping, or generic
  stubs until the baseline result has been recorded.
