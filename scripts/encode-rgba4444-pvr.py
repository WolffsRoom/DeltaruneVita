#!/usr/bin/env python3
"""Encode one PNG as a single-level PVR v3 RGBA4444 texture."""

import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image


PVR3_MAGIC = 0x03525650
# PVR v3 uncompressed channel descriptor: "rgba" followed by 4 bits/channel.
RGBA4444_FORMAT = 0x0404040461626772


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    with Image.open(args.input) as source:
        image = source.convert("RGBA")
        width, height = image.size
        pixels = np.asarray(image, dtype=np.uint16)

    alpha = pixels[:, :, 3]
    # Match the Vita runtime fallback exactly. Non-zero low alpha values remain
    # visible instead of being rounded down to transparent.
    alpha4 = np.where(alpha == 0, 0, np.minimum(15, (alpha + 15) >> 4))
    packed = (((pixels[:, :, 0] >> 4) << 12) |
              ((pixels[:, :, 1] >> 4) << 8) |
              ((pixels[:, :, 2] >> 4) << 4) |
              alpha4).astype("<u2", copy=False)
    payload = packed.tobytes(order="C")

    header = struct.pack(
        "<IIQIIIIIIIII",
        PVR3_MAGIC,
        0,  # flags
        RGBA4444_FORMAT,
        1,  # sRGB
        0,  # unsigned byte normalized
        height,
        width,
        1,  # depth
        1,  # surfaces
        1,  # faces
        1,  # mipmaps
        0,  # metadata bytes
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + payload)


if __name__ == "__main__":
    main()
