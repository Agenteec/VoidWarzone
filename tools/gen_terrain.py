"""Tileable terrain textures for Void Warzone.

Usage: python tools/gen_terrain.py data/terrain
Writes ground.png (scorched plain), soil.png (burnt soil with Void veins), cliff.png (layered rock)
and road.png (concrete plates). All 512x512 and seamless.
"""
import os
import sys

import numpy as np
from PIL import Image

N = 512


def noise(seed, cells, octaves=5, gain=0.5):
    rnd = np.random.default_rng(seed)
    out = np.zeros((N, N))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        c = cells * (2 ** o)
        if c > N:
            break
        grid = rnd.random((c, c))
        img = Image.fromarray((grid * 255).astype(np.uint8)).resize((c * 3, c * 3), Image.NEAREST)
        tiled = np.tile(np.array(Image.fromarray((grid * 255).astype(np.uint8))), (3, 3)).astype(np.float64)
        big = Image.fromarray(tiled.astype(np.uint8)).resize((N * 3, N * 3), Image.BICUBIC)
        layer = np.array(big)[N:2 * N, N:2 * N].astype(np.float64) / 255.0
        out += layer * amp
        total += amp
        amp *= gain
        del img
    return out / total


def ridges(seed, cells):
    return 1.0 - np.abs(noise(seed, cells, 4) * 2.0 - 1.0)


def colorize(t, stops):
    t = np.clip(t, 0.0, 1.0)
    xs = [s[0] for s in stops]
    out = np.zeros((N, N, 3))
    for ch in range(3):
        out[..., ch] = np.interp(t, xs, [s[1][ch] for s in stops])
    return out


def save(path, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(path, optimize=True)


def ground():
    base = noise(1, 4, 6)
    grit = noise(2, 64, 2)
    rgb = colorize(base * 0.75 + grit * 0.25, [(0.0, (70, 62, 54)), (0.45, (108, 96, 80)), (0.75, (132, 118, 96)), (1.0, (150, 138, 116))])
    ash = np.clip((noise(3, 8, 4) - 0.55) * 4.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - ash * 0.25) + np.array([46, 44, 44]) * ash * 0.25
    rgb *= (0.92 + 0.16 * noise(5, 128, 2))[..., None]
    speck = np.random.default_rng(4).random((N, N)) > 0.985
    rgb[speck] *= 0.55
    return rgb


def soil():
    base = noise(11, 4, 6)
    rgb = colorize(base, [(0.0, (34, 26, 26)), (0.5, (62, 46, 42)), (1.0, (88, 66, 56))])
    cracks = ridges(12, 6)
    rgb[cracks > 0.93] *= 0.45
    veins = ridges(13, 3)
    glow = np.clip((veins - 0.975) / 0.025, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - glow * 0.7) + np.array([140, 60, 170]) * glow * 0.7
    return rgb


def cliff():
    y = np.linspace(0, 1, N, endpoint=False)
    warp = noise(21, 4, 5)
    strata = np.sin((y[:, None] + warp * 0.18) * np.pi * 2 * 9) * 0.5 + 0.5
    t = strata * 0.4 + warp * 0.6
    rgb = colorize(t, [(0.0, (52, 48, 46)), (0.4, (86, 80, 74)), (0.7, (112, 104, 94)), (1.0, (132, 124, 112))])
    rgb[ridges(22, 8) > 0.975] *= 0.6
    return rgb


def road():
    base = noise(31, 8, 5)
    rgb = colorize(base, [(0.0, (88, 88, 86)), (0.5, (112, 112, 108)), (1.0, (130, 128, 122))])
    plate = 128
    idx = np.arange(N)
    seam = (idx % plate < 3) | (idx % plate > plate - 3)
    rgb[seam, :] *= 0.55
    rgb[:, seam] *= 0.55
    bolts = np.zeros((N, N), bool)
    for c in (10, plate - 11):
        for r in (10, plate - 11):
            yy, xx = np.ogrid[:N, :N]
            bolts |= (((yy % plate) - r) ** 2 + ((xx % plate) - c) ** 2) < 9
    rgb[bolts] = rgb[bolts] * 0.6 + 40
    rgb *= (0.9 + 0.2 * noise(33, 32, 2))[..., None]
    return rgb


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/terrain"
    os.makedirs(out, exist_ok=True)
    for name, fn in (("ground", ground), ("soil", soil), ("cliff", cliff), ("road", road)):
        save(os.path.join(out, name + ".png"), fn())
    print("terrain textures written to", out)


if __name__ == "__main__":
    main()
