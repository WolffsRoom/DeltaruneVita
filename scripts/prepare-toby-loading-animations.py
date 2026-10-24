#!/usr/bin/env python3
"""Prepare uniformly sized transparent Toby loading animations."""

from pathlib import Path
from urllib.request import urlretrieve
from PIL import Image, ImageSequence


ROOT = Path(__file__).resolve().parents[1]
FRAMES = ROOT / "data" / "frames"
OUTPUT = FRAMES / "animations"
DOWNLOADS = ROOT / ".tmp" / "toby-loading-gifs"
CANVAS = (256, 144)
CONTENT_BOX = (128, 112)
BASELINE_Y = 141

GIFS = {
    "toby_walk": "https://media.tenor.com/x1QOy0SBJW8AAAAj/toby-fox.gif",
    "toby_spin": "https://media.tenor.com/9LCPOb33DnUAAAAj/toby-fox-annoying-dog.gif",
}
LOCAL_DANCE_GIF = OUTPUT / "toby_dance_better" / "toby_dance.gif"


def place_frames(frames, destination: Path):
    destination.mkdir(parents=True, exist_ok=True)
    # Avoid stale frames if a replacement animation has a different length.
    for old in destination.glob("frame_*.png"):
        old.unlink()
    boxes = [frame.getchannel("A").getbbox() for frame in frames]
    boxes = [box for box in boxes if box]
    union = (
        min(box[0] for box in boxes), min(box[1] for box in boxes),
        max(box[2] for box in boxes), max(box[3] for box in boxes),
    )
    source_w, source_h = union[2] - union[0], union[3] - union[1]
    scale = min(CONTENT_BOX[0] / source_w, CONTENT_BOX[1] / source_h)
    size = (max(1, round(source_w * scale)), max(1, round(source_h * scale)))
    for index, frame in enumerate(frames):
        cropped = frame.crop(union).resize(size, Image.Resampling.NEAREST)
        canvas = Image.new("RGBA", CANVAS, (0, 0, 0, 0))
        x = (CANVAS[0] - size[0]) // 2
        y = BASELINE_Y - size[1]
        canvas.alpha_composite(cropped, (x, y))
        canvas.save(destination / f"frame_{index}.png", optimize=True)


def prepare_sleep():
    frames = []
    for path in sorted(FRAMES.glob("frame_*_delay-0.75s.png")):
        luminance = Image.open(path).convert("L")
        rgba = Image.new("RGBA", luminance.size, (255, 255, 255, 0))
        rgba.putalpha(luminance)
        frames.append(rgba)
    if frames:
        place_frames(frames, OUTPUT / "toby_sleep")


def prepare_local_dance():
    if not LOCAL_DANCE_GIF.exists():
        raise FileNotFoundError(f"Missing local Toby dance GIF: {LOCAL_DANCE_GIF}")
    with Image.open(LOCAL_DANCE_GIF) as image:
        frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]
    place_frames(frames, OUTPUT / "toby_dance")


def prepare_gifs():
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    for name, url in GIFS.items():
        source = DOWNLOADS / f"{name.removeprefix('toby_')}.gif"
        if not source.exists():
            urlretrieve(url, source)
        with Image.open(source) as image:
            frames = [frame.convert("RGBA") for frame in ImageSequence.Iterator(image)]
        place_frames(frames, OUTPUT / name)


def main():
    prepare_sleep()
    prepare_local_dance()
    prepare_gifs()
    for folder in sorted(OUTPUT.iterdir()):
        print(folder.name, len(list(folder.glob("frame_*.png"))), "frames")


if __name__ == "__main__":
    main()
