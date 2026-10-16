"""Tileable terrain textures for Void Warzone (default wasteland theme).

Usage: python tools/gen_terrain.py data/terrain
Writes ground.png (scorched plain), soil.png (burnt soil with Void veins), cliff.png (layered rock)
and road.png (concrete plates). 256x256, seamless (fractal Perlin / Worley from tools/texlib.py).
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texlib import N, blend, cell_ids, colorize, fbm, normal_light, ridged, shade, speckle, warp, worley  # noqa: E402


def save(path, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(path, optimize=True)


def ground():
    base = warp(fbm(1, 4, 6), 2, 0.15)
    rgb = colorize(base, [(0.0, (70, 62, 54)), (0.45, (106, 94, 78)), (0.75, (130, 116, 94)), (1.0, (148, 136, 114))])
    pebbles = 1.0 - worley(3, 24)
    rgb = blend(rgb, (60, 54, 48), np.clip((pebbles - 0.7) * 3.0, 0, 1) * 0.5)
    ash = np.clip((fbm(4, 2, 4) - 0.58) * 4.0, 0.0, 1.0)
    rgb = blend(rgb, (46, 44, 44), ash * 0.35)
    rgb = shade(rgb, 0.93 + 0.14 * fbm(5, 32, 2))
    rgb[speckle(6, 0.012)] *= 0.6
    return rgb


def soil():
    base = fbm(11, 4, 6)
    rgb = colorize(base, [(0.0, (34, 26, 26)), (0.5, (62, 46, 42)), (1.0, (88, 66, 56))])
    cracks = worley(12, 10, second=True)
    rgb = shade(rgb, np.where(cracks < 0.06, 0.5, 1.0))
    veins = warp(ridged(13, 3), 14, 0.2)
    glow = np.clip((veins - 0.94) / 0.06, 0.0, 1.0)
    return blend(rgb, (140, 60, 170), glow * 0.7)


def cliff():
    y = np.linspace(0, 1, N, endpoint=False)
    w = fbm(21, 4, 5)
    strata = np.sin((y[:, None] + w * 0.25) * np.pi * 2 * 8) * 0.5 + 0.5
    height = strata * 0.4 + w * 0.6
    rgb = colorize(height, [(0.0, (52, 48, 46)), (0.4, (86, 80, 74)), (0.7, (112, 104, 94)), (1.0, (132, 124, 112))])
    rgb = shade(rgb, normal_light(height, 3.0))
    cracks = worley(22, 6, second=True)
    return shade(rgb, np.where(cracks < 0.04, 0.55, 1.0))


def road():
    plates = 4
    ids = cell_ids(31, plates * 2)
    base = fbm(32, 16, 3)
    rgb = colorize(base * 0.6 + ids * 0.4, [(0.0, (86, 86, 84)), (0.5, (110, 110, 106)), (1.0, (130, 128, 122))])
    idx = np.arange(N)
    seam = (idx % (N // plates) < 2) | (idx % (N // plates) > N // plates - 2)
    rgb[seam, :] *= 0.55
    rgb[:, seam] *= 0.55
    cracks = worley(33, 12, second=True)
    rgb = shade(rgb, np.where(cracks < 0.03, 0.7, 1.0))
    stains = np.clip((fbm(34, 4, 4) - 0.6) * 3.0, 0, 1)
    return blend(rgb, (60, 56, 50), stains * 0.4)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/terrain"
    os.makedirs(out, exist_ok=True)
    for name, fn in (("ground", ground), ("soil", soil), ("cliff", cliff), ("road", road)):
        save(os.path.join(out, name + ".png"), fn())
    print("terrain textures written to", out)


if __name__ == "__main__":
    main()
