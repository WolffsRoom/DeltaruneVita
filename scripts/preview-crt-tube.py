#!/usr/bin/env python3
"""PC preview of the v0.82 CRT Agressivo tube shader (vita_crt_mod_gl.inc,
vita_crt_tube_composite). Applies the same per-pixel math to a 640x480 game
capture placed in the 960x544 Vita frame (4:3 at full height, like the blit).

usage: preview-crt-tube.py <capture.png> <out.png> [--curve 0.075]
"""
import argparse
import numpy as np
from PIL import Image

def tube(src, curve, W=960, H=544):
    tex = np.asarray(src.convert("RGB"), dtype=np.float32) / 255.0
    th, tw = tex.shape[:2]
    effH = H; effW = int(round(tw * H / th))
    sx = (W - effW) // 2
    out = np.zeros((H, W, 3), np.float32)
    ys, xs = np.mgrid[0:H, 0:effW].astype(np.float32)
    v = (ys + 0.5) / H; u = (xs + 0.5) / effW
    cx, cy = u * 2 - 1, v * 2 - 1
    k = (1 + curve * (cx * cx + cy * cy)) / (1 + curve)
    cx, cy = cx * k, cy * k
    uu, vv = cx * 0.5 + 0.5, cy * 0.5 + 0.5
    def ss(e0, e1, x):
        t = np.clip((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t)
    inside = ss(0, 0.004, uu) * ss(1, 0.996, uu) * ss(0, 0.004, vv) * ss(1, 0.996, vv)
    # bilinear sample
    fx = np.clip(uu, 0, 1) * tw - 0.5; fy = np.clip(vv, 0, 1) * th - 0.5
    x0 = np.clip(np.floor(fx).astype(int), 0, tw - 1); y0 = np.clip(np.floor(fy).astype(int), 0, th - 1)
    x1 = np.clip(x0 + 1, 0, tw - 1); y1 = np.clip(y0 + 1, 0, th - 1)
    ax = np.clip(fx - x0, 0, 1)[..., None]; ay = np.clip(fy - y0, 0, 1)[..., None]
    col = (tex[y0, x0] * (1 - ax) + tex[y0, x1] * ax) * (1 - ay) + (tex[y1, x0] * (1 - ax) + tex[y1, x1] * ax) * ay
    s = np.sin(vv * (th * 0.5) * np.pi); scan = (0.74 + 0.26 * s * s)[..., None]
    m = np.mod(np.floor(xs + sx), 3)
    mask = 0.88 + 0.12 * np.stack([(m <= 0.5), (m >= 0.5) & (m <= 1.5), (m >= 1.5)], -1).astype(np.float32)
    e = (uu * (1 - uu)) * (vv * (1 - vv))
    vig = np.clip(np.power(np.maximum(e * 16, 0), 0.22), 0, 1)[..., None]
    glare = (0.035 * (1 - ss(0, 0.22, vv)) * (1 - np.abs(cx)))[..., None]
    res = (col * 1.14 * scan * mask * vig + glare) * inside[..., None]
    out[:, sx:sx + effW] = np.clip(res, 0, 1)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8))

if __name__ == "__main__":
    ap = argparse.ArgumentParser(); ap.add_argument("src"); ap.add_argument("out")
    ap.add_argument("--curve", type=float, default=0.075)
    a = ap.parse_args()
    tube(Image.open(a.src), a.curve).save(a.out)
