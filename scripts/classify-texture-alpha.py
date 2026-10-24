#!/usr/bin/env python3
"""Choose RGBA5551 when binary alpha is safe, otherwise RGBA4444."""

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--max-intermediate-visible-percent", type=float, default=0.1)
    args = parser.parse_args()

    with Image.open(args.input) as source:
        alpha = np.asarray(source.convert("RGBA"), dtype=np.uint8)[:, :, 3]
    visible = alpha > 0
    intermediate = visible & (alpha < 255)
    percent = 100.0 * int(intermediate.sum()) / max(1, int(visible.sum()))
    texture_format = "rgba5551" if percent <= args.max_intermediate_visible_percent else "rgba4444"
    print(f"{texture_format},{percent:.6f},{int(intermediate.sum())},{int(visible.sum())}")


if __name__ == "__main__":
    main()
