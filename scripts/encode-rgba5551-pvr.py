#!/usr/bin/env python3
"""Encode one PNG as a single-level PVR v3 RGBA5551 texture."""

import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image


PVR3_MAGIC = 0x03525650
# PVR v3 channel descriptor: "rgba" followed by 5, 5, 5 and 1 bits.
RGBA5551_FORMAT = 0x0105050561626772


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with Image.open(args.input) as source:
        image = source.convert("RGBA")
        width, height = image.size
        pixels = np.asarray(image, dtype=np.uint16)

    # Alpha is binary in RGBA5551. A midpoint threshold gives predictable
    # cutouts and avoids retaining nearly transparent compression noise.
    alpha1 = (pixels[:, :, 3] >= 128).astype(np.uint16)
    packed = (((pixels[:, :, 0] >> 3) << 11) |
              ((pixels[:, :, 1] >> 3) << 6) |
              ((pixels[:, :, 2] >> 3) << 1) |
              alpha1).astype("<u2", copy=False)

    header = struct.pack(
        "<IIQIIIIIIIII",
        PVR3_MAGIC,
        0,
        RGBA5551_FORMAT,
        1,  # sRGB
        0,  # unsigned byte normalized
        height,
        width,
        1,
        1,
        1,
        1,
        0,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + packed.tobytes(order="C"))


if __name__ == "__main__":
    main()
