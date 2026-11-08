#!/usr/bin/env python3
"""Generate frames_side.bani for each animated border.

Only the left and right 120 columns of a 960x544 frame are stored, because the
4:3 game image covers everything between them. The result is 240x544 RGB565.
The borders have no transparency, so RGB565 loses nothing but colour depth, and
it bands less than the RGBA4444 of frames.bani and frames_fast.bani.

Format: 12-byte header, then the frames as raw little-endian uint16, top row
first. Each row is the 120 left columns followed by the 120 right columns.
    'BANS', width u16, height u16, frame count u16, frame duration in ms u16

Every folder in data/Deltarune Borders Enhanced that has a GIF and its numbered
PNG frames is converted; the runtime animates whichever borders have a stream.
Frame timing comes from the GIF. Up-to-date outputs are skipped. Source frames
and the older frames.bani and frames_fast.bani streams are only read.

Usage: generate-animated-border-sides.py [--check]
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SOURCE_ROOT = os.path.join(ROOT, "data", "Deltarune Borders Enhanced")
TARGET_ROOT = os.path.join(ROOT, "data", "mods", "bordersanim")
SIDE_COLUMNS = 120
FRAME_W, FRAME_H = 960, 544
OUT_W = SIDE_COLUMNS * 2
FRAME_BYTES = OUT_W * FRAME_H * 2


def read_header(path):
    with open(path, "rb") as stream:
        return struct.unpack("<4sHHHH", stream.read(12))


def border_names():
    names = []
    for current, dirs, files in os.walk(SOURCE_ROOT):
        dirs.sort()
        name = os.path.basename(current)
        if f"{name}.gif" in files and source_frames(name, current):
            names.append((name, current))
    names.sort(key=lambda item: item[0])
    return names


def gif_timing(name, source_dir):
    with Image.open(os.path.join(source_dir, f"{name}.gif")) as gif:
        count = getattr(gif, "n_frames", 1)
        durations = []
        for index in range(count):
            gif.seek(index)
            durations.append(gif.info.get("duration", 100))
    return count, max(20, round(sum(durations) / len(durations)))


def source_frames(name, source_dir=None):
    source_dir = source_dir or os.path.join(SOURCE_ROOT, name)
    pattern = os.path.join(glob.escape(source_dir), f"{name}_*.png")
    paths = [p for p in glob.glob(pattern)
             if os.path.splitext(p)[0].rsplit("_", 1)[1].isdigit()]
    if not paths:
        pattern = os.path.join(glob.escape(source_dir), "frames", "frame_*.png")
        paths = [p for p in glob.glob(pattern)
                 if os.path.splitext(p)[0].rsplit("_", 1)[1].isdigit()]
    paths.sort(key=lambda p: int(os.path.splitext(p)[0].rsplit("_", 1)[1]))
    return paths


def encode_frame(path):
    image = Image.open(path).convert("RGB").resize((FRAME_W, FRAME_H), Image.LANCZOS)
    pixels = np.asarray(image, dtype=np.uint16)
    sides = np.concatenate((pixels[:, :SIDE_COLUMNS], pixels[:, FRAME_W - SIDE_COLUMNS:]), axis=1)
    red = (sides[..., 0] * 31 + 127) // 255
    green = (sides[..., 1] * 63 + 127) // 255
    blue = (sides[..., 2] * 31 + 127) // 255
    return ((red << 11) | (green << 5) | blue).astype("<u2").tobytes()


def is_current(name, source_dir, target):
    if not os.path.exists(target):
        return False
    newest = max(os.path.getmtime(p) for p in source_frames(name, source_dir))
    return os.path.getmtime(target) >= newest


def generate(name, source_dir):
    target_dir = os.path.join(TARGET_ROOT, name)
    target = os.path.join(target_dir, "frames_side.bani")
    if is_current(name, source_dir, target):
        print(f"{name}: up to date")
        return
    count, delay = gif_timing(name, source_dir)
    paths = source_frames(name, source_dir)
    if len(paths) != count:
        raise SystemExit(f"{name}: {len(paths)} PNG frames, the GIF has {count}")
    if not 2 <= count <= 120 or delay > 1000:
        raise SystemExit(f"{name}: {count} frames at {delay} ms is outside the runtime limits")
    os.makedirs(target_dir, exist_ok=True)
    temporary = target + ".tmp"
    with open(temporary, "wb") as out:
        out.write(struct.pack("<4sHHHH", b"BANS", OUT_W, FRAME_H, count, delay))
        for path in paths:
            frame = encode_frame(path)
            assert len(frame) == FRAME_BYTES
            out.write(frame)
    os.replace(temporary, target)
    print(f"{name}: {count} frames, {delay} ms, {os.path.getsize(target)} bytes")


def check(name, source_dir):
    target = os.path.join(TARGET_ROOT, name, "frames_side.bani")
    if not os.path.exists(target):
        print(f"{name}: missing")
        return False
    magic, width, height, count, delay = read_header(target)
    expected = 12 + count * FRAME_BYTES
    problems = []
    if (magic, width, height) != (b"BANS", OUT_W, FRAME_H):
        problems.append(f"header {magic} {width}x{height}")
    if count != len(source_frames(name, source_dir)):
        problems.append(f"{count} frames, {len(source_frames(name, source_dir))} in the source")
    if os.path.getsize(target) != expected:
        problems.append(f"size {os.path.getsize(target)} != {expected}")
    print(f"{name}: {'OK' if not problems else '; '.join(problems)}")
    return not problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate existing files only")
    args = parser.parse_args()
    if args.check:
        return 0 if all([check(name, source_dir) for name, source_dir in border_names()]) else 1
    for name, source_dir in border_names():
        generate(name, source_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
