#!/usr/bin/env python3
"""GXM21 conservative tile-range property regression (no visual deletions).

Tests random affine camera matrices and translated/mirrored/sheared sprites.
For every original tile whose projected rectangle can intersect a pixel of
the target, optimized index bounds must retain that tile. Separately checks
affine AABB identity vs 4-corner projection for arbitrary signed rectangles.
"""
import math
import random

rng = random.Random(20261013)
EPS = 1e-3


def bbox(m, x, y, w, h):
    px = [m[0] * a + m[2] * b + m[4] for a, b in
          ((x, y), (x + w, y), (x, y + h), (x + w, y + h))]
    py = [m[1] * a + m[3] * b + m[5] for a, b in
          ((x, y), (x + w, y), (x, y + h), (x + w, y + h))]
    return min(px), min(py), max(px), max(py)


def bbox_fast(m, x, y, w, h):
    ox = m[0] * x + m[2] * y + m[4]
    oy = m[1] * x + m[3] * y + m[5]
    ax, bx = m[0] * w, m[2] * h
    ay, by = m[1] * w, m[3] * h
    return (ox + min(0, ax) + min(0, bx),
            oy + min(0, ay) + min(0, by),
            ox + max(0, ax) + max(0, bx),
            oy + max(0, ay) + max(0, by))


def intersects_view(m, x, y, w, h, width, height):
    local=((x,y),(x+w,y),(x+w,y+h),(x,y+h))
    quad=[(m[0]*a+m[2]*b+m[4],
           m[1]*a+m[3]*b+m[5]) for a,b in local]
    viewport=[(-2,-2),(width+2,-2),(width+2,height+2),(-2,height+2)]
    # Convex polygon SAT (projected bounding boxes alone are not enough:
    # a sheared or rotated offscreen rect can have an overlapping AABB).
    axes=[(1,0),(0,1)]
    for i in (0,1):
        ax=quad[(i+1)%4][0]-quad[i][0]
        ay=quad[(i+1)%4][1]-quad[i][1]
        axes.append((-ay,ax))
    for ax,ay in axes:
        q=[ax*p[0]+ay*p[1] for p in quad]
        v=[ax*p[0]+ay*p[1] for p in viewport]
        if max(q)<min(v) or max(v)<min(q):
            return False
    return True


def inverse(m):
    a, b, c, d, tx, ty = m
    det = a * d - c * b
    if abs(det) < 1e-7:
        return None
    return (d / det, -b / det, -c / det, a / det,
            (c * ty - d * tx) / det, (b * tx - a * ty) / det)


def span(origin, step, limit, lo, hi):
    if step <= 0 or limit <= origin:
        return (0, 0)
    n = math.ceil((limit - origin) / step)
    if not math.isfinite(lo) or not math.isfinite(hi):
        return 0, n
    first = math.floor((lo - origin) / step) - 2
    last = math.ceil((hi - origin) / step) + 2
    start = max(0, min(n, first))
    return start, max(start, min(n, last))


tested = 0
saved = 0
for _ in range(3000):
    w = rng.choice([640, 960, 1280])
    h = rng.choice([480, 544])
    angle = rng.uniform(-math.pi, math.pi)
    scale_x = rng.uniform(.2, 3) * rng.choice([-1, 1])
    scale_y = rng.uniform(.2, 3) * rng.choice([-1, 1])
    shear = rng.uniform(-.3, .3)
    m = (math.cos(angle) * scale_x, math.sin(angle) * scale_x,
         -math.sin(angle) * scale_y + shear,
         math.cos(angle) * scale_y,
         rng.uniform(-450, 450), rng.uniform(-340, 340))
    inv = inverse(m)
    if inv is None:
        continue
    vis = bbox(inv, -3, -3, w + 6, h + 6)
    tx, ty = rng.uniform(-700, 100), rng.uniform(-700, 100)
    step_x, step_y = rng.uniform(9, 110), rng.uniform(9, 110)
    nx, ny = rng.randint(1, 32), rng.randint(1, 32)
    lim_x, lim_y = tx + nx * step_x, ty + ny * step_y
    ox, oy = rng.uniform(-140, 140), rng.uniform(-140, 140)
    sprite_w, sprite_h = rng.uniform(1, 160), rng.uniform(1, 160)
    pad_x = sprite_w + abs(ox) + 2 * step_x + 3
    pad_y = sprite_h + abs(oy) + 2 * step_y + 3
    sx0, sx1 = span(tx, step_x, lim_x, vis[0] - pad_x, vis[2] + pad_x)
    sy0, sy1 = span(ty, step_y, lim_y, vis[1] - pad_y, vis[3] + pad_y)
    tested += nx * ny
    saved += nx * ny - (sx1 - sx0) * (sy1 - sy0)
    for iy in range(ny):
        for ix in range(nx):
            rx = tx + ix * step_x - ox
            ry = ty + iy * step_y - oy
            rect = bbox(m, rx, ry, sprite_w, sprite_h)
            intersects = intersects_view(m,rx,ry,sprite_w,sprite_h,w,h)
            if intersects:
                assert sx0 <= ix < sx1 and sy0 <= iy < sy1, (
                    ix, iy, (sx0, sx1, sy0, sy1), rect, m,
                    vis,pad_x,pad_y,tx,ty,step_x,step_y,nx,ny,rx,ry,ox,oy,sprite_w,sprite_h)

for _ in range(25000):
    m = [rng.uniform(-8, 8) for _ in range(6)]
    x, y, ww, hh = [rng.uniform(-1200, 1200) for _ in range(4)]
    expected = bbox(m, x, y, ww, hh)
    actual = bbox_fast(m, x, y, ww, hh)
    for a, b in zip(expected, actual):
        assert abs(a - b) <= max(EPS, abs(a)*1e-9), (expected, actual)
print(f"GXM21 visibility lossless property PASS: {tested:,} tiles,"
      f" {saved:,} safe candidates eliminated, 25,000 affine AABB cases")
