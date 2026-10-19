"""Tileable terrain textures for Void Warzone (default wasteland theme).

Usage: python tools/gen_terrain.py data/terrain
Writes ground.png (scorched plain), soil.png (burnt soil with Void veins), cliff.png (layered rock)
and road.png (concrete plates). 256x256, seamless: Poly Haven photo bases (tools/fetch_assets.py) + texlib noise.
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from photolib import level, photo  # noqa: E402
from texlib import N, blend, cell_ids, colorize, fbm, normal_light, ridged, shade, speckle, warp, worley  # noqa: E402


def save(path, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(path, optimize=True)


def ground():
    rgb = level(photo("burned_ground_01", repeat=2, sat=0.8), 98)
    ash = np.clip((fbm(4, 2, 4) - 0.58) * 4.0, 0.0, 1.0)
    return blend(rgb, (46, 44, 44), ash * 0.3)


def soil():
    rgb = level(photo("brown_mud_rocks_01", repeat=2, sat=0.6, tint=(200, 170, 170)), 58)
    veins = warp(ridged(13, 3), 14, 0.2)
    glow = np.clip((veins - 0.94) / 0.06, 0.0, 1.0)
    return blend(rgb, (140, 60, 170), glow * 0.7)


def cliff():
    rgb = level(photo("rock_ground", repeat=2, sat=0.5), 92)
    cracks = worley(22, 6, second=True)
    return shade(rgb, np.where(cracks < 0.035, 0.6, 1.0))


def road():
    plates = 4
    rgb = level(photo("concrete_debris", repeat=2, sat=0.3), 108)
    idx = np.arange(N)
    seam = (idx % (N // plates) < 2) | (idx % (N // plates) > N // plates - 2)
    rgb[seam, :] *= 0.55
    rgb[:, seam] *= 0.55
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
