# PVR hybrid texture set (v0.65 experimental)

Place only approved, visually tested atlas pages here:

`chapterN/page_000.pvr`

PT-BR data uses `chapterN-ptbr/page_000.pvr`. The page number must match the
TXTR index of that exact `data.win`. Accepted formats are RGBA PVRTC1 4 BPP and
RGBA PVRTC2 4 BPP in a PVR v3 container, encoded in sRGB. Missing or invalid files fall back to
the embedded GameMaker texture.

Do not externalize fonts, UI, dialogue frames, touch assets, puzzle atlases or
anything used to build/read a surface until it has been verified on hardware.
Run `scripts/validate-pvr-assets.ps1` before preparing the Vita data.
