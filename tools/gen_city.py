"""Winter terrain and city facade textures for Void Warzone.

Usage: python tools/gen_city.py data
Writes data/terrain/winter/{ground,soil,cliff,road}.png (seamless snow / trampled snow / frosted rock / slushy asphalt)
and data/city/{panel,brick,industrial,house,roof,slate}.png plus *_mask.png (G = lit windows) for extruded buildings.
Facades are 8 bays x 8 floors per tile; the client maps one tile to 8 window bays and 8 storeys.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_terrain import N, colorize, noise, ridges, save  # noqa: E402

T = 512
CELL = T // 8


def winter_ground():
    base = noise(201, 4, 6)
    drift = noise(202, 2, 4)
    rgb = colorize(base * 0.6 + drift * 0.4, [(0.0, (196, 204, 216)), (0.5, (222, 228, 236)), (1.0, (240, 243, 248))])
    sastrugi = np.clip((ridges(203, 6) - 0.88) * 6.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - sastrugi * 0.12) + np.array([170, 182, 200]) * sastrugi * 0.12
    rgb *= (0.97 + 0.06 * noise(204, 128, 2))[..., None]
    grass = np.random.default_rng(205).random((N, N)) > 0.996
    rgb[grass] = rgb[grass] * 0.5 + np.array([70, 64, 50]) * 0.5
    return rgb


def winter_soil():
    base = noise(211, 6, 5)
    rgb = colorize(base, [(0.0, (150, 150, 156)), (0.45, (184, 188, 196)), (1.0, (214, 218, 226))])
    mud = np.clip((noise(212, 8, 4) - 0.66) * 4.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - mud * 0.45) + np.array([86, 78, 70]) * mud * 0.45
    tracks = np.clip((ridges(213, 10) - 0.9) * 8.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - tracks * 0.25)
    return rgb


def winter_cliff():
    y = np.linspace(0, 1, N, endpoint=False)
    warp = noise(221, 4, 5)
    strata = np.sin((y[:, None] + warp * 0.2) * np.pi * 2 * 7) * 0.5 + 0.5
    rgb = colorize(strata * 0.4 + warp * 0.6, [(0.0, (60, 60, 64)), (0.5, (96, 96, 100)), (1.0, (126, 126, 130))])
    snow = np.clip((noise(222, 6, 4) - 0.45) * 3.0, 0.0, 1.0)[..., None]
    return rgb * (1.0 - snow * 0.7) + np.array([226, 232, 240]) * snow * 0.7


def winter_road():
    base = noise(231, 32, 3)
    rgb = colorize(base, [(0.0, (70, 70, 74)), (0.5, (80, 80, 84)), (1.0, (92, 92, 96))])
    cracks = ridges(232, 6)
    rgb[cracks > 0.97] *= 0.7
    slush = np.clip((noise(233, 12, 3) - 0.58) * 4.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1.0 - slush * 0.55) + np.array([176, 182, 192]) * slush * 0.55
    ruts = np.zeros(N)
    x = np.arange(N)
    for c in (N * 0.3, N * 0.7):
        ruts += np.exp(-((x - c) / 12.0) ** 2)
    rgb = rgb * (1.0 - 0.35 * ruts[None, :, None]) + np.array([58, 56, 54]) * 0.35 * ruts[None, :, None]
    return rgb


def stains(seed, strength):
    s = noise(seed, 16, 3)
    streak = np.repeat(np.random.default_rng(seed).random((1, T)), T, axis=0)
    streak = np.array(Image.fromarray((streak * 255).astype(np.uint8)).resize((T, T), Image.BILINEAR)) / 255.0
    return 1.0 - strength * np.clip(s * 0.6 + streak * 0.6 - 0.45, 0.0, 1.0)


def windows(img, mask, rnd, frame, glass, lit_chance, wide=0.5, tall=0.48, sill=True, skip=0.0, top=0.22):
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
            d.rectangle((x0 - 3, y0 - 3, x1 + 3, y1 + 3), fill=frame)
            lit = rnd.random() < lit_chance
            if lit:
                warm = (int(255 * (0.85 + 0.15 * rnd.random())), int(190 + 40 * rnd.random()), int(90 + 50 * rnd.random()))
                d.rectangle((x0, y0, x1, y1), fill=warm)
                m.rectangle((x0, y0, x1, y1), fill=(0, 255, 0))
            else:
                shade = glass if rnd.random() > 0.15 else (20, 20, 22)
                d.rectangle((x0, y0, x1, y1), fill=shade)
                d.line((x0, y0 + 2, x1, y0 + 2), fill=tuple(min(255, c + 30) for c in shade), width=2)
            d.line(((x0 + x1) // 2, y0, (x0 + x1) // 2, y1), fill=frame, width=3)
            if sill:
                d.rectangle((x0 - 4, y1 + 3, x1 + 4, y1 + 7), fill=(232, 236, 242))


def panel():
    rnd = np.random.default_rng(301)
    base = colorize(noise(302, 8, 4), [(0.0, (150, 146, 138)), (0.5, (172, 168, 158)), (1.0, (188, 184, 172))])
    base *= stains(303, 0.35)[..., None]
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    for i in range(0, T, CELL):
        d.line((0, i, T, i), fill=(110, 106, 100), width=3)
        d.line((i, 0, i, T), fill=(120, 116, 110), width=2)
    for bx in (1, 6):
        for fy in range(8):
            if rnd.random() < 0.8:
                x0, y0 = bx * CELL + 4, fy * CELL + int(CELL * 0.7)
                d.rectangle((x0, y0, x0 + CELL - 8, y0 + int(CELL * 0.26)), fill=(196, 194, 188) if rnd.random() < 0.6 else (120, 128, 120))
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (210, 208, 202), (54, 62, 72), 0.14)
    return img, mask


def brick():
    rnd = np.random.default_rng(311)
    base = colorize(noise(312, 32, 3), [(0.0, (110, 56, 44)), (0.5, (136, 70, 52)), (1.0, (156, 86, 64))])
    rows = np.arange(T) % 8 < 1
    base[rows, :] = (178, 170, 160)
    for r in range(0, T, 8):
        off = 8 if (r // 8) % 2 else 0
        base[r:r + 8, (np.arange(T) + off) % 16 == 0] = (178, 170, 160)
    base *= stains(313, 0.3)[..., None]
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (190, 190, 186), (50, 58, 66), 0.1, wide=0.56, tall=0.5)
    return img, mask


def industrial():
    rnd = np.random.default_rng(321)
    x = np.arange(T)
    ribs = (np.sin(x / T * np.pi * 2 * 48) * 0.5 + 0.5)[None, :]
    base = colorize(noise(322, 8, 4) * 0.7 + ribs * 0.3, [(0.0, (88, 98, 104)), (0.5, (112, 122, 128)), (1.0, (134, 142, 146))])
    rust = np.clip((noise(323, 8, 4) - 0.6) * 3.0, 0.0, 1.0)[..., None]
    base = base * (1 - rust * 0.6) + np.array([120, 70, 40]) * rust * 0.6
    base *= stains(324, 0.4)[..., None]
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    m = ImageDraw.Draw(mask)
    for fy in (1, 5):
        y0 = fy * CELL + 8
        for bx in range(8):
            lit = rnd.random() < 0.08
            box = (bx * CELL + 6, y0, bx * CELL + CELL - 6, y0 + 18)
            d.rectangle(box, fill=(255, 205, 120) if lit else (40, 48, 56))
            if lit:
                m.rectangle(box, fill=(0, 255, 0))
    return img, mask


def house():
    rnd = np.random.default_rng(331)
    y = np.arange(T)
    planks = ((y % 16) < 2)[:, None]
    paint = [(70, 92, 74), (96, 70, 52), (74, 84, 110)][1]
    base = colorize(noise(332, 32, 3), [(0.0, tuple(int(c * 0.75) for c in paint)), (1.0, tuple(int(c * 1.15) for c in paint))])
    base = np.where(planks[..., None], base * 0.6, base)
    base *= stains(333, 0.3)[..., None]
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8))
    mask = Image.new("RGB", (T, T), (0, 0, 0))
    windows(img, mask, rnd, (228, 226, 214), (40, 46, 52), 0.18, wide=0.42, tall=0.46, skip=0.35, top=0.25)
    return img, mask


def roof():
    rgb = colorize(noise(341, 6, 5), [(0.0, (190, 198, 210)), (0.6, (226, 232, 240)), (1.0, (242, 245, 250))])
    tar = np.clip((noise(342, 4, 4) - 0.68) * 5.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1 - tar * 0.7) + np.array([50, 50, 54]) * tar * 0.7
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)), Image.new("RGB", (T, T), (0, 0, 255))


def slate():
    y = np.arange(T)
    rgb = colorize(noise(351, 8, 4), [(0.0, (196, 204, 214)), (1.0, (238, 242, 248))])
    rows = ((y % 32) < 3)[:, None]
    bare = np.clip((noise(352, 6, 4) - 0.62) * 4.0, 0.0, 1.0)[..., None]
    rgb = rgb * (1 - bare * 0.6) + np.array([96, 100, 104]) * bare * 0.6
    rgb = np.where(rows[..., None], rgb * 0.82, rgb)
    return Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)), Image.new("RGB", (T, T), (0, 0, 255))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data"
    terrain = os.path.join(out, "terrain", "winter")
    city = os.path.join(out, "city")
    os.makedirs(terrain, exist_ok=True)
    os.makedirs(city, exist_ok=True)
    for name, fn in (("ground", winter_ground), ("soil", winter_soil), ("cliff", winter_cliff), ("road", winter_road)):
        save(os.path.join(terrain, name + ".png"), fn())
    for name, fn in (("panel", panel), ("brick", brick), ("industrial", industrial), ("house", house), ("roof", roof), ("slate", slate)):
        img, mask = fn()
        img.save(os.path.join(city, name + ".png"), optimize=True)
        mask.save(os.path.join(city, name + "_mask.png"), optimize=True)
    Image.new("RGB", (8, 8), (0, 0, 255)).save(os.path.join(city, "flat_mask.png"))
    print("winter terrain and city textures written to", out)


if __name__ == "__main__":
    main()
