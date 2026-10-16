"""Seamless procedural noise for texture generation (numpy + perlin-numpy).

pip install numpy pillow perlin-numpy
All functions return float arrays in 0..1 that tile in both directions.
"""
import numpy as np
from perlin_numpy import generate_fractal_noise_2d, generate_perlin_noise_2d

N = 256


def pow2(res):
    return 1 << max(0, int(round(np.log2(max(res, 1)))))


def perlin(seed, res, size=N):
    res = pow2(res)
    np.random.seed(seed)
    return np.clip(generate_perlin_noise_2d((size, size), (res, res), tileable=(True, True)) * 0.7 + 0.5, 0.0, 1.0)


def fbm(seed, res, octaves=5, persistence=0.5, size=N):
    res = pow2(res)
    np.random.seed(seed)
    while res * 2 ** (octaves - 1) > size:
        octaves -= 1
    n = generate_fractal_noise_2d((size, size), (res, res), octaves, persistence, tileable=(True, True))
    return np.clip(n * 0.6 + 0.5, 0.0, 1.0)


def ridged(seed, res, octaves=4, size=N):
    return 1.0 - np.abs(fbm(seed, res, octaves, size=size) * 2.0 - 1.0)


def worley(seed, cells, size=N, second=False):
    """Distance to the nearest (or F2 - F1 edge distance) jittered point on a periodic grid, normalised by cell size."""
    rnd = np.random.default_rng(seed)
    pts = (np.arange(cells)[:, None, None] * 0 + 0.0)
    jitter = rnd.random((cells, cells, 2))
    step = size / cells
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
    cy = (yy // step).astype(int)
    cx = (xx // step).astype(int)
    f1 = np.full((size, size), 1e9)
    f2 = np.full((size, size), 1e9)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            gy = cy + dy
            gx = cx + dx
            j = jitter[gy % cells, gx % cells]
            py = (gy + j[..., 0]) * step
            px = (gx + j[..., 1]) * step
            d = np.hypot(py - yy, px - xx) / step
            f2 = np.where(d < f1, f1, np.minimum(f2, d))
            f1 = np.minimum(f1, d)
    del pts
    if second:
        return np.clip(f2 - f1, 0.0, 1.0)
    return np.clip(f1, 0.0, 1.0)


def cell_ids(seed, cells, size=N):
    """Random value per Worley cell (for per-stone / per-plate colour variation)."""
    rnd = np.random.default_rng(seed)
    jitter = rnd.random((cells, cells, 2))
    value = rnd.random((cells, cells))
    step = size / cells
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
    cy = (yy // step).astype(int)
    cx = (xx // step).astype(int)
    best = np.full((size, size), 1e9)
    out = np.zeros((size, size))
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            gy, gx = cy + dy, cx + dx
            j = jitter[gy % cells, gx % cells]
            d = np.hypot((gy + j[..., 0]) * step - yy, (gx + j[..., 1]) * step - xx)
            closer = d < best
            best = np.where(closer, d, best)
            out = np.where(closer, value[gy % cells, gx % cells], out)
    return out


def warp(field, seed, amount, res=4):
    """Domain warp: resample `field` at coordinates displaced by two noise fields (wraps around)."""
    size = field.shape[0]
    ox = (fbm(seed, res, 3, size=size) - 0.5) * amount * size
    oy = (fbm(seed + 1, res, 3, size=size) - 0.5) * amount * size
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float64)
    x = (xx + ox) % size
    y = (yy + oy) % size
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx = x - x0
    fy = y - y0
    x1 = (x0 + 1) % size
    y1 = (y0 + 1) % size
    a = field[y0, x0] * (1 - fx) + field[y0, x1] * fx
    b = field[y1, x0] * (1 - fx) + field[y1, x1] * fx
    return a * (1 - fy) + b * fy


def colorize(t, stops):
    t = np.clip(t, 0.0, 1.0)
    xs = [s[0] for s in stops]
    out = np.zeros(t.shape + (3,))
    for ch in range(3):
        out[..., ch] = np.interp(t, xs, [s[1][ch] for s in stops])
    return out


def blend(rgb, color, amount):
    a = np.clip(amount, 0.0, 1.0)[..., None]
    return rgb * (1.0 - a) + np.array(color, dtype=np.float64) * a


def shade(rgb, factor):
    return rgb * np.asarray(factor)[..., None]


def speckle(seed, chance, size=N):
    return np.random.default_rng(seed).random((size, size)) < chance


def normal_light(height, strength=2.0, light=(-0.6, -0.6)):
    """Fake directional lighting from a height field (baked relief)."""
    gx = np.roll(height, -1, 1) - np.roll(height, 1, 1)
    gy = np.roll(height, -1, 0) - np.roll(height, 1, 0)
    return np.clip(1.0 + (gx * light[0] + gy * light[1]) * strength, 0.6, 1.4)
