"""Winter terrain and city facade textures for Void Warzone.

Usage: python tools/gen_city.py data
Writes data/terrain/winter/{ground,soil,cliff,road}.png (snow / trampled snow / frosted rock / slushy asphalt)
and data/city/{panel,ruin,brick,industrial,house,garage,roof,slate,rubble}.png plus *_mask.png
(G = lit windows, B = no shader snow). Facades are 8 bays x 8 storeys per tile; the client maps one tile
to 8 window bays and 8 storeys. All 256x256 and seamless (tools/texlib.py).
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from texlib import N, blend, cell_ids, colorize, fbm, normal_light, perlin, ridged, shade, speckle, warp, worley  # noqa: E402

T = N
CELL = T // 8


def save(path, rgb):
    Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8), "RGB").save(path, optimize=True)


def winter_ground():
    drift = warp(fbm(201, 3, 5), 202, 0.25)
    ripples = warp(np.sin(np.linspace(0, np.pi * 2 * 12, N)[None, :] + fbm(203, 4, 3) * 6.0) * 0.5 + 0.5, 204, 0.08)
    height = drift * 0.8 + ripples * 0.2
    height = drift * 0.9 + ripples * 0.1
    rgb = colorize(height, [(0.0, (196, 204, 218)), (0.5, (220, 227, 236)), (1.0, (238, 242, 247))])
    rgb = shade(rgb, normal_light(height, 1.5))
    tufts = (1.0 - worley(205, 12)) * (fbm(206, 4, 3) > 0.7)
    rgb = blend(rgb, (92, 86, 68), np.clip((tufts - 0.75) * 5.0, 0, 1) * 0.6)
    sparkle = speckle(207, 0.004)
    rgb[sparkle] = (252, 253, 255)
    return rgb


def winter_soil():
    base = fbm(211, 6, 5)
    rgb = colorize(base, [(0.0, (150, 150, 156)), (0.45, (182, 186, 194)), (1.0, (212, 216, 224))])
    lumps = 1.0 - worley(212, 14)
    rgb = shade(rgb, 0.9 + 0.15 * lumps)
    mud = np.clip((warp(fbm(213, 4, 4), 214, 0.2) - 0.64) * 4.0, 0.0, 1.0)
    rgb = blend(rgb, (84, 76, 68), mud * 0.5)
    x = np.arange(N)
    ruts = np.zeros(N)
    for c in (N * 0.28, N * 0.72):
        ruts += np.exp(-((x - c) / 7.0) ** 2)
    ruts = warp(np.repeat(ruts[None, :], N, 0), 215, 0.04)
    return blend(rgb, (110, 106, 104), ruts * 0.35)


def winter_cliff():
    y = np.linspace(0, 1, N, endpoint=False)
    w = fbm(221, 4, 5)
    strata = np.sin((y[:, None] + w * 0.22) * np.pi * 2 * 6) * 0.5 + 0.5
    height = strata * 0.35 + w * 0.65
    rgb = colorize(height, [(0.0, (58, 58, 62)), (0.5, (92, 92, 98)), (1.0, (124, 124, 130))])
    rgb = shade(rgb, normal_light(height, 3.0))
    rgb = shade(rgb, np.where(worley(222, 7, second=True) < 0.04, 0.55, 1.0))
    snow = np.clip((fbm(223, 5, 4) - 0.48) * 3.5, 0.0, 1.0)
    return blend(rgb, (226, 232, 240), snow * 0.75)


def winter_road():
    grain = fbm(231, 32, 3)
    rgb = colorize(grain, [(0.0, (64, 64, 68)), (0.5, (76, 76, 80)), (1.0, (90, 90, 94))])
    patches = cell_ids(232, 6)
    rgb = shade(rgb, 0.92 + 0.12 * patches)
    rgb = shade(rgb, np.where(worley(233, 9, second=True) < 0.025, 0.65, 1.0))
    slush = np.clip((warp(fbm(234, 6, 4), 235, 0.2) - 0.56) * 4.0, 0.0, 1.0)
    rgb = blend(rgb, (178, 184, 194), slush * 0.6)
    holes = np.clip((1.0 - worley(236, 5) - 0.86) * 8.0, 0, 1) * (fbm(237, 3, 2) > 0.55)
    return blend(rgb, (40, 40, 44), holes * 0.6)


def weathering(seed, strength):
    s = fbm(seed, 8, 3)
    streak = np.repeat(np.random.default_rng(seed).random((1, T)), T, axis=0)
    streak = np.array(Image.fromarray((streak * 255).astype(np.uint8)).resize((T, T), Image.BILINEAR)) / 255.0
    drip = np.clip(np.linspace(0.6, 1.2, T)[:, None] * streak * 0.7 + s * 0.6 - 0.55, 0.0, 1.0)
    return 1.0 - strength * drip


def windows(img, mask, rnd, frame, glass, lit_chance, wide=0.5, tall=0.48, sill=True, skip=0.0, top=0.22, broken=0.22, boarded=0.07):
    d = ImageDraw.Draw(img)
    m = ImageDraw.Draw(mask)
    for fy in range(8):
        for bx in range(8):
            if rnd.random() < skip:
                continue
            x0 = bx * CELL + int(CELL * (1 - wide) / 2)
            y0 = fy * CELL + int(CELL * top)
            x1 = x0 + int(CELL * wide)
            y1 = y0 + int(CELL * tall)
            d.rectangle((x0 - 2, y0 - 2, x1 + 2, y1 + 2), fill=frame)
            if rnd.random() < broken:
                d.rectangle((x0, y0, x1, y1), fill=(14, 12, 12))
                d.line((x0, y0, x1, y1 - 3), fill=(30, 30, 32), width=1)
                d.rectangle((x0 - 2, y0 - 6, x1 + 2, y0 - 2), fill=(28, 26, 26))
                continue
            if rnd.random() < boarded:
                d.rectangle((x0, y0, x1, y1), fill=(84, 66, 48))
                for k in range(y0 + 2, y1, 4):
                    d.line((x0, k, x1, k + (1 if rnd.random() < 0.5 else -1)), fill=(58, 44, 32))
                continue
            if rnd.random() < lit_chance:
                warm = (int(240 + 15 * rnd.random()), int(180 + 50 * rnd.random()), int(90 + 50 * rnd.random()))
                d.rectangle((x0, y0, x1, y1), fill=warm)
                m.rectangle((x0, y0, x1, y1), fill=(0, 255, 0))
            else:
                g = glass if rnd.random() > 0.2 else (22, 22, 24)
                d.rectangle((x0, y0, x1, y1), fill=g)
                d.line((x0, y0 + 1, x1, y0 + 1), fill=tuple(min(255, c + 34) for c in g), width=1)
            d.line(((x0 + x1) // 2, y0, (x0 + x1) // 2, y1), fill=frame, width=2)
            if sill:
                d.rectangle((x0 - 2, y1 + 2, x1 + 2, y1 + 4), fill=(232, 236, 242))


def concrete(seed, stops, panels=True):
    base = fbm(seed, 8, 4) * 0.7 + cell_ids(seed + 1, 8) * 0.3
    rgb = colorize(base, stops)
    rgb = shade(rgb, weathering(seed + 2, 0.35))
    if panels:
        idx = np.arange(T)
        rgb[idx % CELL < 1, :] *= 0.7
        rgb[:, idx % CELL < 1] *= 0.78
    return rgb


def to_img(rgb):
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8))


def panel():
    rnd = np.random.default_rng(301)
    img = to_img(concrete(302, [(0.0, (146, 142, 134)), (0.5, (168, 164, 154)), (1.0, (186, 182, 170))]))
    d = ImageDraw.Draw(img)
    for bx in (1, 6):
        for fy in range(8):
            if rnd.random() < 0.8:
                x0, y0 = bx * CELL + 2, fy * CELL + int(CELL * 0.72)
                d.rectangle((x0, y0, x0 + CELL - 4, y0 + int(CELL * 0.24)), fill=(194, 192, 186) if rnd.random() < 0.6 else (116, 126, 118))
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (208, 206, 200), (44, 50, 58), 0.01)
    return img, mask


def soot(img, mask, rnd, plumes):
    """Burnt flats: black soot plumes rising from broken windows."""
    rgb = np.array(img, dtype=np.float64)
    m = np.array(mask)
    yy, xx = np.mgrid[0:T, 0:T]
    noise = fbm(int(rnd.integers(0, 10000)), 8, 4)
    for _ in range(plumes):
        bx, fy = int(rnd.integers(0, 8)), int(rnd.integers(1, 8))
        cx = bx * CELL + CELL / 2
        y0 = fy * CELL + CELL * 0.7
        h = CELL * rnd.uniform(1.6, 3.0)
        t = np.clip((y0 - yy) / h, 0, 1)
        width = CELL * (0.32 + 0.55 * t)
        inside = (yy <= y0) & (yy >= y0 - h) & (np.abs(xx - cx + (noise - 0.5) * CELL * 0.5) < width)
        dark = np.where(inside, np.clip((1.0 - t) ** 0.5 * (0.75 + 0.5 * noise), 0, 1), 0.0)
        rgb = rgb * (1.0 - dark[..., None] * 0.92) + np.array([14, 12, 12]) * dark[..., None] * 0.92
        wy0, wx0 = int(fy * CELL + CELL * 0.22), int(bx * CELL + CELL * 0.25)
        rgb[wy0:wy0 + int(CELL * 0.48), wx0:wx0 + int(CELL * 0.5)] = (10, 9, 9)
        m[wy0:wy0 + int(CELL * 0.48), wx0:wx0 + int(CELL * 0.5)] = 0
    return to_img(rgb), Image.fromarray(m)


def panel_burnt():
    img, mask = panel()
    return soot(img, mask, np.random.default_rng(391), 7)


def brick_burnt():
    img, mask = brick()
    return soot(img, mask, np.random.default_rng(392), 6)


def ruin():
    rnd = np.random.default_rng(305)
    rgb = concrete(306, [(0.0, (92, 90, 86)), (0.5, (118, 114, 108)), (1.0, (138, 134, 126))])
    soot = np.clip((warp(fbm(307, 4, 4), 308, 0.3) - 0.45) * 2.5, 0.0, 1.0)
    rgb = blend(rgb, (30, 28, 28), soot * 0.75)
    img = to_img(rgb)
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (90, 88, 84), (20, 20, 22), 0.0, broken=0.7, boarded=0.1)
    return img, mask


def brick():
    rnd = np.random.default_rng(311)
    rows, cols = 32, 16
    yy, xx = np.mgrid[0:T, 0:T]
    row = yy // (T // rows)
    shift = (row % 2) * (T // cols // 2)
    col = (xx + shift) // (T // cols)
    tone = np.random.default_rng(312).random((rows, cols + 1))[row, col % (cols + 1)]
    rgb = colorize(tone * 0.5 + fbm(313, 16, 3) * 0.5, [(0.0, (104, 52, 42)), (0.5, (132, 68, 52)), (1.0, (156, 88, 66))])
    mortar = (yy % (T // rows) < 1) | ((xx + shift) % (T // cols) < 1)
    rgb[mortar] = (176, 168, 158)
    rgb = shade(rgb, weathering(314, 0.3))
    img = to_img(rgb)
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (190, 190, 186), (42, 48, 54), 0.008, wide=0.56, tall=0.5)
    return img, mask


def industrial():
    rnd = np.random.default_rng(321)
    x = np.arange(T)
    ribs = (np.sin(x / T * np.pi * 2 * 32) * 0.5 + 0.5)[None, :] * np.ones((T, 1))
    rgb = colorize(fbm(322, 8, 4) * 0.7 + ribs * 0.3, [(0.0, (86, 96, 102)), (0.5, (110, 120, 126)), (1.0, (132, 140, 144))])
    rust = np.clip((warp(fbm(323, 6, 4), 324, 0.2) - 0.6) * 3.0, 0.0, 1.0)
    rgb = blend(rgb, (120, 70, 40), rust * 0.6)
    rgb = shade(rgb, weathering(325, 0.4))
    img = to_img(rgb)
    d = ImageDraw.Draw(img)
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    m = ImageDraw.Draw(mask)
    for fy in (1, 5):
        y0 = fy * CELL + 4
        for bx in range(8):
            lit = rnd.random() < 0.01
            box = (bx * CELL + 3, y0, bx * CELL + CELL - 3, y0 + 9)
            d.rectangle(box, fill=(255, 205, 120) if lit else (40, 48, 56))
            if lit:
                m.rectangle(box, fill=(0, 255, 0))
    return img, mask


def house():
    rnd = np.random.default_rng(331)
    y = np.arange(T)
    planks = ((y % 8) < 1)[:, None] * np.ones((1, T), bool)
    paint = (96, 70, 52)
    rgb = colorize(fbm(332, 16, 3), [(0.0, tuple(int(c * 0.75) for c in paint)), (1.0, tuple(int(c * 1.15) for c in paint))])
    rgb[planks] *= 0.6
    rgb = shade(rgb, weathering(333, 0.3))
    img = to_img(rgb)
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (228, 226, 214), (36, 40, 46), 0.015, wide=0.42, tall=0.46, skip=0.35, top=0.25, boarded=0.15)
    return img, mask


def garage():
    rnd = np.random.default_rng(341)
    rgb = colorize(fbm(342, 8, 4), [(0.0, (120, 112, 104)), (1.0, (150, 144, 134))])
    rgb = shade(rgb, weathering(343, 0.35))
    img = to_img(rgb)
    d = ImageDraw.Draw(img)
    paints = [(70, 88, 78), (86, 74, 60), (60, 72, 90), (110, 60, 42), (96, 98, 96)]
    for fy in range(8):
        for bx in range(8):
            x0, x1 = bx * CELL + 3, bx * CELL + CELL - 3
            y0, y1 = fy * CELL + 6, fy * CELL + CELL
            c = paints[int(rnd.random() * len(paints))]
            d.rectangle((x0, y0, x1, y1), fill=c)
            for k in range(y0 + 3, y1, 4):
                d.line((x0, k, x1, k), fill=tuple(int(v * 0.8) for v in c))
            d.line(((x0 + x1) // 2, y0, (x0 + x1) // 2, y1), fill=tuple(int(v * 0.6) for v in c))
            if rnd.random() < 0.5:
                rx, ry = x0 + int(rnd.random() * 18), y0 + int(rnd.random() * 18)
                d.ellipse((rx, ry, rx + 6, ry + 8), fill=(118, 64, 36))
            d.rectangle((x0 - 2, y0 - 3, x1 + 2, y0 - 1), fill=(228, 232, 238))
    return img, Image.new("RGB", (T, T), (0, 0, 0))


def roof():
    rgb = colorize(warp(fbm(351, 5, 5), 352, 0.2), [(0.0, (188, 196, 208)), (0.6, (224, 230, 238)), (1.0, (242, 245, 250))])
    tar = np.clip((fbm(353, 4, 4) - 0.66) * 5.0, 0.0, 1.0)
    rgb = blend(rgb, (50, 50, 54), tar * 0.7)
    return to_img(rgb), Image.new("RGB", (T, T), (0, 0, 255))


def slate():
    y = np.arange(T)
    rgb = colorize(fbm(361, 8, 4), [(0.0, (196, 204, 214)), (1.0, (238, 242, 248))])
    rows = ((y % 16) < 2)[:, None] * np.ones((1, T), bool)
    bare = np.clip((fbm(362, 6, 4) - 0.62) * 4.0, 0.0, 1.0)
    rgb = blend(rgb, (96, 100, 104), bare * 0.6)
    rgb[rows] *= 0.82
    return to_img(rgb), Image.new("RGB", (T, T), (0, 0, 255))


def rubble():
    chunks = 1.0 - worley(371, 18)
    ids = cell_ids(372, 18)
    height = chunks * 0.7 + fbm(373, 8, 3) * 0.3
    rgb = colorize(ids * 0.6 + height * 0.4, [(0.0, (70, 68, 66)), (0.5, (110, 106, 100)), (1.0, (150, 146, 138))])
    rgb = shade(rgb, normal_light(height, 4.0))
    rebar = ridged(374, 6) > 0.97
    rgb[rebar] = (90, 50, 30)
    snow = np.clip((fbm(375, 4, 4) - 0.5) * 3.0, 0, 1) * np.clip(height * 1.5 - 0.3, 0, 1)
    rgb = blend(rgb, (226, 232, 240), snow * 0.8)
    return to_img(rgb), Image.new("RGB", (T, T), (0, 0, 255))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data"
    terrain = os.path.join(out, "terrain", "winter")
    city = os.path.join(out, "city")
    os.makedirs(terrain, exist_ok=True)
    os.makedirs(city, exist_ok=True)
    for name, fn in (("ground", winter_ground), ("soil", winter_soil), ("cliff", winter_cliff), ("road", winter_road)):
        save(os.path.join(terrain, name + ".png"), fn())
    for name, fn in (("panel", panel), ("panel_burnt", panel_burnt), ("brick_burnt", brick_burnt), ("ruin", ruin), ("brick", brick), ("industrial", industrial), ("house", house), ("garage", garage),
                     ("roof", roof), ("slate", slate), ("rubble", rubble)):
        img, mask = fn()
        img.save(os.path.join(city, name + ".png"), optimize=True)
        mask.save(os.path.join(city, name + "_mask.png"), optimize=True)
    Image.new("RGB", (8, 8), (0, 0, 255)).save(os.path.join(city, "flat_mask.png"))
    print("winter terrain and city textures written to", out)


if __name__ == "__main__":
    main()
