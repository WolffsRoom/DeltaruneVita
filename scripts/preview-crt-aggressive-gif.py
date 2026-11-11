#!/usr/bin/env python3
"""PC preview GIF of CRT Agressivo (v0.83-6): border art, the game picture through
the tube shader (vita_crt_tube_composite) and, on the border strips, the
scanline/grille overlay (vita_crt_mod_render fragment, strength 1.15).

usage: preview-crt-aggressive-gif.py <border.png> <out.gif> <frame1.png> [frame2.png ...]
"""
import sys
import importlib.util
from pathlib import Path
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("tube", HERE / "preview-crt-tube.py")
tube = importlib.util.module_from_spec(spec); spec.loader.exec_module(tube)

W, H = 960, 544
NATIVE = "--native" in sys.argv
if NATIVE: sys.argv.remove("--native")

def overlay(img, t, strength=1.15):
    """vita_crt_mod_gl.inc fragment: multiply by scanline, grille, vignette, grain."""
    a = np.asarray(img, dtype=np.float32) / 255.0
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    u = (xs + 0.5) / W; v = 1.0 - (ys + 0.5) / H          # GL uv (bottom-up)
    px = np.floor(np.stack([u * 960.0, v * 544.0], -1))
    scan = 1.0 - 0.18 * np.mod(px[..., 1], 2.0)
    m = np.mod(px[..., 0], 3.0)
    mask = 0.90 + 0.10 * np.stack([(m <= 0.5), (m >= 0.5) & (m <= 1.5), (m >= 1.5)], -1).astype(np.float32)
    e = (u * (1 - u)) * (v * (1 - v))
    edge = 1.0 - np.clip(e * 17.0, 0, 1)
    vig = 1.0 - 0.30 * edge * edge
    cell = np.floor(px / 2.0) + np.stack([np.full_like(u, np.floor(t * 7.0) * 5.0), np.zeros_like(u)], -1)
    grain = np.mod(52.9829189 * np.mod(cell[..., 0] * 0.06711056 + cell[..., 1] * 0.00583715, 1.0), 1.0)
    effect = mask * (scan * vig * (0.98 + 0.02 * grain))[..., None]
    factor = 1.0 + (effect - 1.0) * strength
    return np.clip(a * factor, 0, 1)

def frame(border, game, t):
    out = border.convert("RGB").resize((W, H), Image.NEAREST)
    base = overlay(out, t)                                # borders: overlay
    if NATIVE:
        # In-game "native_centered": the 640x480 picture at 1:1 in the middle
        # of the 960x544 screen, border visible above and below (rect 160,32).
        pic = tube.tube(game.resize((640, 480), Image.NEAREST), 0.075, 640, 480)
        sx, sy = (W - 640) // 2, (H - 480) // 2
        base[sy:sy + 480, sx:sx + 640] = np.asarray(pic, dtype=np.float32)[:480, :640] / 255.0
        return Image.fromarray((base * 255 + 0.5).astype(np.uint8))
    pic = tube.tube(game, 0.075)                                # 960x544, black outside the 4:3 picture
    pic_a = np.asarray(pic, dtype=np.float32) / 255.0
    th, tw = game.size[1], game.size[0]
    effW = int(round(tw * H / th)); sx = (W - effW) // 2
    base[:, sx:sx + effW] = pic_a[:, sx:sx + effW]        # picture: tube
    return Image.fromarray((base * 255 + 0.5).astype(np.uint8))

def main():
    border, out, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
    b = Image.open(border)
    imgs = [frame(b, Image.open(f), k / 30.0).convert("P", palette=Image.ADAPTIVE, colors=255)
            for k, f in enumerate(frames)]
    imgs[0].save(out, save_all=True, append_images=imgs[1:], duration=66, loop=0, optimize=False)
    print(out, len(imgs))

if __name__ == "__main__":
    main()
