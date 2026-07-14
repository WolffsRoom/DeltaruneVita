# DELTARUNE Vita

Experimental PlayStation Vita compatibility project for an Android GameMaker
port of **DELTARUNE Chapters 1–5**.

> [!IMPORTANT]
> This project is currently **awaiting testing on real PS Vita hardware**. The
> available work is a test baseline, not a confirmed playable release.

## Current status

`IMPLEMENTED — AWAITING HARDWARE TEST`

Static analysis of the target Android package confirmed:

- a 32-bit ARMv7 `libyoyo.so`;
- ELF32, ARM EABI5, soft-float ABI;
- GameMaker bytecode in `assets/game.droid`;
- game data split across `chapter0.wad` through `chapter5.wad`;
- standard YoYo Runner JNI entry points;
- separate OGG music and MP4 video assets.

These properties make the package structurally suitable for
[YoYo Loader Vita](https://github.com/Rinnegatamante/yoyoloader_vita). YoYo
Loader already contains a DELTARUNE keymap and a DELTARUNE-specific
`glReadPixels` workaround.

This does **not** yet prove that all chapters, audio, video, saving, or controls
work correctly on the Vita.

## Project files

- [YoYo Loader Nightly VPK](downloads/YoYoLoader-Nightly.vpk) — prebuilt
  baseline used for the first hardware test;
- [YoYo Loader builder](downloads/yoyoloader-builder.zip) — upstream standalone
  launcher builder and binaries;
- [DLTVITA-0001 source package](downloads/yoyoloader-deltarune-DLTVITA-0001-source.zip)
  — instrumented source awaiting a VitaSDK-softfp build;
- [Baseline test instructions](BASELINE-INSTRUCTIONS.md);
- [DLTVITA-0001 technical report](DLTVITA-0001-REPORT.md);
- [Portuguese installation note](downloads/INSTALL-PT-BR.txt).

The APK and game assets are intentionally not included. After downloading the
project files, supply your own compatible APK and rename it to `game.apk` as
described below.

## Installation layout

Install the official YoYo Loader VPK, then place your legally obtained Android
package on the Vita using this layout:

```text
ux0:data/gms/deltarune/
├── game.apk
└── yyl.cfg
```

Recommended baseline `yyl.cfg`:

```ini
forceGLES1=0
forceBilinear=0
platTarget=0
debugShaders=0
debugMode=1
noSplash=0
maximizeMem=0
netSupport=0
squeezeMem=0
disableAudio=0
uncachedMem=0
doubleBuffering=0
```

For the first run, do not optimize the APK or externalize its assets. A clean
baseline makes crashes and loading failures easier to diagnose.

## Requirements

- A homebrew-enabled PlayStation Vita;
- [YoYo Loader Vita](https://github.com/Rinnegatamante/yoyoloader_vita);
- `kubridge.skprx`;
- `fd_fix.skprx`, unless rePatch is installed;
- `libshacccg.suprx`;
- a legally obtained compatible Android package.

Follow the upstream YoYo Loader documentation when installing its plugins and
runtime dependencies.

## Testing and logs

After one test run, retrieve:

```text
ux0:data/gms/shared/yyl.log
```

If the application crashes, also preserve the `psp2core` dump from that same
run. Do not mix a log from one build with a core dump from another.

Useful expected log markers are:

```text
Detected DELTARUNE as Game ID
Enabling Deltarune specific gamehack!
Startup ended
```

When reporting a result, include:

- Vita model and firmware;
- YoYo Loader build/version;
- the last relevant lines of `yyl.log`;
- whether the menu, gameplay, audio, video, controls, and saving worked;
- the chapter tested;
- the matching core dump when applicable.

## Known risks

- The Android package uses custom `WADLoader` and data-provider Java classes.
  YoYo Loader may need a native replacement for specific methods.
- The combined game data is roughly 700 MiB. Storage size alone is acceptable,
  but loading large WADs may exceed available memory.
- MP4 playback may need additional work or chapter-specific bypasses.
- A newer GameMaker runner can require JNI, shader, or import compatibility
  changes.

## Development

Game-specific loader changes require a **VitaSDK-softfp** environment. The
upstream project builds through the `atamanenko/vitasdk-softfp` container and
also compiles soft-float versions of VitaGL, OpenAL Soft, OpenSL ES,
SceShaccCgExt, and VitaShaRK.

The proprietary Sony SDK is not a replacement for VitaSDK-softfp and is not
used by this project.

Development follows evidence-based revision rules:

1. Use an explicit Build ID for every test build.
2. Make one small change per revision.
3. Preserve a new log for every run.
4. Match logs and core dumps from the same test.
5. Do not add generic JNI or native stubs without understanding their ABI and
   side effects.
6. Do not claim a fix works until it is confirmed on hardware.

## Legal notice

This repository does not provide DELTARUNE, an APK, `libyoyo.so`, WAD files,
music, videos, or other proprietary game data. Users must supply their own
legally obtained files.

DELTARUNE is created by Toby Fox. This is an unofficial, fan-made compatibility
project and is not affiliated with or endorsed by Toby Fox, 8-4, GameMaker,
Sony Interactive Entertainment, or the YoYo Loader developers.

## Credits

- [Rinnegatamante](https://github.com/Rinnegatamante) and the YoYo Loader Vita
  contributors;
- [TheFloW](https://github.com/TheOfficialFloW) for the original Android `.so`
  loading work used by many Vita ports;
- the VitaSDK, VitaGL, VitaShaRK, and Vita Nuova communities.
