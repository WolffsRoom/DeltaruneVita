#!/usr/bin/env python3
"""PC preview of CRT Agressivo v0.84-4 (vita_crt_full_present): border + game
composed at 960x544 (game 640x480 centred, as in-game), then the whole screen
curved: warped sampling, precomputed mask (tube edge x vignette x grille x
gain) and scanlines on the game's rows.

usage: preview-crt-full.py <border.png> <out.(png|gif)> <frame1.png> [frame2.png ...]
"""
import sys
import numpy as np
from PIL import Image

W, H, CURVE, GAIN = 960, 544, 0.075, 1.5

def warp(x, y):
    k = (1 + CURVE * (x * x + y * y)) / (1 + CURVE)
    return x * k * 0.5 + 0.5, y * k * 0.5 + 0.5

def mask():
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    nx = (xs + 0.5) / W * 2 - 1
    ny = 1 - (ys + 0.5) / H * 2                 # image rows top-down -> GL up
    u, v = warp(nx, ny)
    def edge(t):
        return np.where((t <= 0) | (t >= 1), 0, np.where(t < 0.004, t / 0.004, np.where(t > 0.996, (1 - t) / 0.004, 1)))
    e = np.clip(u * (1 - u) * v * (1 - v) * 16, 0, None)
    vig = np.clip(np.power(e, 0.22), 0, 1)
    base = edge(u) * edge(v) * vig * 1.24 / GAIN
    g = np.full((H, W, 3), 0.88, np.float32)
    m = (xs.astype(int) % 3)
    for c in range(3):
        g[..., c][m == c] = 1.0
    return np.clip(base[..., None] * g, 0, 1), u, v

MASK, U, V = mask()

def frame(border, game):
    comp = border.convert("RGB").resize((W, H), Image.NEAREST)
    comp.paste(game.convert("RGB").resize((640, 480), Image.NEAREST), ((W - 640) // 2, (H - 480) // 2))
    src = np.asarray(comp, dtype=np.float32) / 255.0
    # bilinear sample at the warped coordinate (GL v up -> image row down)
    fx = np.clip(U, 0, 1) * W - 0.5
    fy = (1 - np.clip(V, 0, 1)) * H - 0.5
    x0 = np.clip(np.floor(fx).astype(int), 0, W - 1); y0 = np.clip(np.floor(fy).astype(int), 0, H - 1)
    x1 = np.clip(x0 + 1, 0, W - 1); y1 = np.clip(y0 + 1, 0, H - 1)
    ax = np.clip(fx - x0, 0, 1)[..., None]; ay = np.clip(fy - y0, 0, 1)[..., None]
    col = (src[y0, x0] * (1 - ax) + src[y0, x1] * ax) * (1 - ay) + (src[y1, x0] * (1 - ax) + src[y1, x1] * ax) * ay
    # scanlines: 240 game rows over the 480-pixel game band (sy=32, ey=512)
    sy, ey = (H - 480) // 2, (H - 480) // 2 + 480
    t = ((ey - V * H) / (ey - sy)) * 240.0
    ph = np.mod(t, 1.0)
    prof = np.array([0.55, 0.775, 1.0, 0.775, 0.55], np.float32)
    pos = ph * 4 - 0.5
    i0 = np.floor(pos).astype(int); fr = pos - i0
    scan = prof[np.mod(i0, 4)] * (1 - fr) + prof[np.mod(i0 + 1, 4)] * fr
    out = col * MASK * (scan[..., None] * GAIN)
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype(np.uint8))

def main():
    border, out, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
    b = Image.open(border)
    imgs = [frame(b, Image.open(f)) for f in frames]
    if out.lower().endswith(".gif"):
        pal = [im.convert("P", palette=Image.ADAPTIVE, colors=255) for im in imgs]
        pal[0].save(out, save_all=True, append_images=pal[1:], duration=66, loop=0)
    else:
        imgs[0].save(out)
    print(out, len(imgs))

if __name__ == "__main__":
    main()
