#!/usr/bin/env python3
"""Generate the external first_load sequence as full-screen indexed PNGs."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data" / "frames" / "first_load"
OUTPUT = ROOT / "data" / "prepared" / "deltarune" / "deltarunevita" / "ui" / "first_load"
FRAME_SIZE = (960, 544)
FRAME_COUNT = 133


def build_shared_palette(files):
    # Sample every frame so palette transitions stay stable across the sequence.
    cols = 8
    thumb_size = (240, 136)
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new("RGB", (thumb_size[0] * cols, thumb_size[1] * rows), (0, 0, 0))
    for i, path in enumerate(files):
        with Image.open(path) as im:
            if im.size != FRAME_SIZE:
                raise RuntimeError(f"{path.name}: expected {FRAME_SIZE}, got {im.size}")
            thumb = im.convert("RGB").resize(thumb_size, Image.Resampling.LANCZOS)
        sheet.paste(thumb, ((i % cols) * thumb_size[0], (i // cols) * thumb_size[1]))
    quantized = sheet.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    palette = Image.new("P", (1, 1))
    palette.putpalette(quantized.getpalette())
    return palette


def main():
    files = [SOURCE / f"frame_{i:03d}.png" for i in range(FRAME_COUNT)]
    missing = [p.name for p in files if not p.exists()]
    if missing:
        raise RuntimeError(f"missing first_load frames: {missing[:5]}")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    palette = build_shared_palette(files)
    total = 0
    for path in files:
        with Image.open(path) as im:
            out = im.convert("RGB").quantize(palette=palette, dither=Image.Dither.NONE)
        target = OUTPUT / path.name
        out.save(target, optimize=True)
        total += target.stat().st_size
    print(f"generated={FRAME_COUNT} size=960x544 mode=P bytes={total}")


if __name__ == "__main__":
    main()
