"""Converts real-world CC0 vehicle models into Void Warzone glb files (see data/models/CREDITS.md).

python tools/import_models.py [data/models]
Needs Blender (VW_BLENDER or the default install path), numpy, pillow, perlin-numpy.
Sources are downloaded into tools/models_src/ (gitignored). Each model is normalised (front = -z, up = +y,
length in model units, centred, standing on y = 0), decimated, split into hull / turret / barrel and either
remapped onto the shared palette (flat-coloured sources) or given its own weathered texture plus a team mask.
"""
import io
import json
import math
import os
import subprocess
import sys
import urllib.request
import zipfile

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_models as gm  # noqa: E402
import texlib  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "models_src")
BLENDER = os.environ.get("VW_BLENDER", r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe")
OGA = "https://opengameart.org/sites/default/files/"

SOURCES = {
    "ru": OGA + "russian_vehicles_lowepoly_pixelart.zip",
    "uaz452": OGA + "uaz-452_onlymodelandtex.zip",
    "t26": OGA + "t-12.zip",
    "jeep": OGA + "Torque3D_Jeep.zip",
}

RU = "russian_vehicles_lowepoly_pixelart/"

MODELS = {
    # player units: hull only, the turret comes from the colony model of the same unit (visual.model.turret)
    "real_uaz469": dict(src="ru", file="bobik/bobik.obj", texture="bobik/bobik_albedo.png", length=2.2, wear=0.35, team=True,
                        pivot_at=0.55),
    "real_uaz452": dict(src="uaz452", file="uaz_452.fbx", length=2.6, wear=0.4, team=True, team_material="UazMat", pivot_at=0.45, flip=False,
                        drop=["LicensePlate"]),
    "real_vaz2106": dict(src="ru", file="vaz/vaz_klassika.obj", texture="vaz/vaz2106_albedo_red.png", length=2.1, wear=0.7, team=False,
                         pivot_at=0.55),
    "real_jeep_gun": dict(src="jeep", file="Torque3D_Jeep/source_files/jeep.blend", length=2.4, palette=True,
                          decimate={"Jeep5": 0.12, "jeep_wheel": 0.15, "jeep_gun": 0.3},
                          parts={"turret": ["jeep_gun"]}, barrel_cut=0.08,
                          colors={"green_metal": "paint_old", "Olive": "paint_old", "rubber": "rubber", "tread": "rubber",
                                  "Glass": "glass_dark", "Mirror": "glass_dark", "Leather": "wood", "Steel": "graphite",
                                  "Aluminum": "steel", "Embossed": "graphite", "Color_A06": "paint_red", "FrontColor": "lamp"}),
    "real_t26": dict(src="t26", file="T-12 soviet tank.fbx", length=2.5, palette=True, decimate={"*": 0.14},
                     parts={"turret": ["TOWER"]}, barrel_cut=0.25,
                     colors={"camo": "team_dark", "black metal": "graphite_dark", "brown metal": "rust", "grey metal": "steel"}),
    # city wrecks (replace the procedural cars and buses)
    "prop_car1": dict(src="ru", file="vaz/vaz_klassika.obj", texture="vaz/vaz2101_albedo_grey.png", length=2.3, wear=1.0),
    "prop_car2": dict(src="ru", file="vaz/vaz_klassika.obj", texture="vaz/vaz2105_albedo_green.png", length=2.3, wear=1.0),
    "prop_car3": dict(src="ru", file="vaz/vaz_klassika.obj", texture="vaz/vaz2106_albedo_blue.png", length=2.3, wear=1.0),
    "prop_car4": dict(src="ru", file="bobik/bobik.obj", texture="bobik/bobik_albedo.png", length=2.2, wear=1.0),
    "prop_car5": dict(src="ru", file="buhanka/buhanka.obj", texture="buhanka/buhanka_grey_albedo.png", length=2.4, wear=1.0),
    "prop_car_burnt1": dict(src="ru", file="vaz/vaz_klassika.obj", texture="vaz/vaz2105_albedo_baklaj.png", length=2.3, wear=2.0),
    "prop_car_burnt2": dict(src="uaz452", file="uaz_destroyed.fbx", length=2.5, wear=1.6, flip=False),
    "prop_bus": dict(src="ru", file="bus/bus.obj", texture="bus/bus_yellow_albedo.png", length=5.2, wear=1.1),
    "prop_bus_burnt": dict(src="ru", file="bus/bus.obj", texture="bus/bus_red_albedo.png", length=5.2, wear=2.0),
}


def ph(name, size, wear=0.3, budget=600, **kw):
    return dict(src="ph:" + name, file=name + ".gltf", length=2.0, wear=wear, budget=budget, flip=False, **kw)


PROPS = {
    "prop_barrel": ph("barrel_03", 0.9), "prop_barrel_rust": ph("Barrel_01", 0.9, wear=0.6), "prop_tyre": ph("old_tyre", 1.0),
    "prop_trashcan": ph("metal_trash_can", 0.8), "prop_trashbag": ph("trashbag", 0.8), "prop_utility1": ph("utility_box_01", 1.0),
    "prop_utility2": ph("utility_box_02", 1.0), "prop_barrier": ph("concrete_road_barrier_02", 1.6, budget=500),
    "prop_covered_car": ph("covered_car", 2.3, budget=800, wear=0.5), "prop_crate_mil": ph("wooden_military_crate", 1.0),
    "prop_crate_old": ph("old_military_crate", 0.9), "prop_propane": ph("propane_tank", 0.7), "prop_stove": ph("barrel_stove", 0.9),
    "prop_firepit": ph("stone_fire_pit", 1.2), "prop_stump": ph("tree_stump_01", 1.0, budget=400), "prop_deadtree": ph("dead_tree_trunk", 3.0, budget=900),
    "prop_rock_ph": ph("rock_07", 1.6, budget=400), "prop_jerrycan": ph("metal_jerrycan_green", 0.5),
    "prop_generator": ph("portable_generator", 1.0), "prop_manhole": ph("water_manhole_cover", 0.8, budget=200),
}


def fetch(key):
    if key.startswith("ph:"):
        root = os.path.join(HERE, "assets_src", "polyhaven", key[3:])
        if not os.path.isdir(root):
            raise FileNotFoundError(root + " (run tools/fetch_assets.py)")
        return root
    root = os.path.join(CACHE, key)
    if os.path.isdir(root):
        return root
    os.makedirs(CACHE, exist_ok=True)
    print("downloading", SOURCES[key])
    req = urllib.request.Request(SOURCES[key], headers={"User-Agent": "Mozilla/5.0"})
    data = urllib.request.urlopen(req, timeout=120).read()
    zipfile.ZipFile(io.BytesIO(data)).extractall(root)
    return root


def find(root, rel):
    direct = os.path.join(root, rel)
    if os.path.exists(direct):
        return direct
    base = os.path.basename(rel).lower()
    for d, _, files in os.walk(root):
        for f in files:
            if f.lower() == base:
                return os.path.join(d, f)
    return None


def dump(name, cfg):
    root = fetch(cfg["src"])
    path = find(root, cfg["file"])
    if not path:
        raise FileNotFoundError(cfg["file"])
    out = os.path.join(CACHE, "dump_" + name + ".json")
    job = os.path.join(CACHE, "job_" + name + ".json")
    with open(job, "w", encoding="utf-8") as f:
        json.dump({"file": path, "out": out, "drop": cfg.get("drop", []), "decimate": cfg.get("decimate", {}), "budget": cfg.get("budget", 0)}, f)
    subprocess.run([BLENDER, "-b", "--factory-startup", "--disable-autoexec", "--python", os.path.join(HERE, "blender_dump.py"), "--", job],
                   check=True, stdout=subprocess.DEVNULL)
    with open(out, encoding="utf-8") as f:
        return json.load(f), root


def srgb(c):
    return tuple(int(round(255 * (x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055))) for x in c)


def nearest_material(rgb):
    best, bd = "graphite", 1e9
    for name, col, team, glow in gm.MATERIALS:
        if team or glow:
            continue
        d = sum((a - b) ** 2 for a, b in zip(rgb, col))
        if d < bd:
            best, bd = name, d
    return best


def normalise(objects, length):
    """Blender (x, y, z-up, front +y) -> glTF (x, y-up, front -z), scaled and centred."""
    pts = np.array([v[:3] for o in objects for t in o["tris"] for v in t])
    conv = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=float)
    pts = pts @ conv.T
    lo, hi = pts.min(0), pts.max(0)
    # the longest horizontal axis is the vehicle's length; front is decided by the configured flip
    along_x = (hi[0] - lo[0]) > (hi[2] - lo[2])
    scale = length / max(hi[0] - lo[0], hi[2] - lo[2])
    centre = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    return conv, along_x, scale, centre


def transform(v, conv, along_x, scale, centre, flip):
    p = conv @ np.array(v[:3]) - centre
    n = conv @ np.array(v[3:6])
    if along_x:
        p = np.array([p[2], p[1], -p[0]])
        n = np.array([n[2], n[1], -n[0]])
    if flip:
        p = np.array([-p[0], p[1], -p[2]])
        n = np.array([-n[0], n[1], -n[2]])
    return p * scale, n


def weather(img, wear, seed):
    """Fades, dirties and rusts a texture; wear 0 = field-worn, 1 = abandoned, 2 = burnt out."""
    w, h = img.size
    k = max(1, 256 // max(w, h))
    img = img.resize((w * k, h * k), Image.NEAREST)
    a = np.asarray(img.convert("RGB"), dtype=float) / 255.0
    H, W = a.shape[:2]
    S = texlib.pow2(max(H, W))

    def field(f):
        return np.asarray(Image.fromarray((f * 255).astype(np.uint8)).resize((W, H), Image.BILINEAR), dtype=float)[..., None] / 255.0

    grime = field(texlib.fbm(seed, 4, size=S))
    spots = field(texlib.fbm(seed + 7, 16, size=S))
    gray = a.mean(-1, keepdims=True)
    fade = min(0.08 + 0.2 * wear, 0.6)
    a = a * (1 - fade) + gray * fade
    a *= (1.0 - 0.06 * min(wear, 1.5)) * (0.88 + 0.12 * grime)
    rust = np.array([0.42, 0.22, 0.12])
    thr = 0.74 - 0.12 * min(wear, 1.5)
    r = np.clip((spots - thr) * 5.0, 0, 1) * (0.35 + 0.3 * min(wear, 1.0))
    a = a * (1 - r) + rust * r
    if wear >= 1.5:
        char = np.array([0.1, 0.085, 0.075])
        burn = np.clip(0.55 + (grime - 0.5) * 2.0, 0.25, 0.92)
        a = a * (1 - burn) + char * burn
        a = a * (1 - r * 0.6) + rust * 0.8 * (r * 0.6)
    return Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)), k


def team_mask(img, k, rows=None):
    """R = body paint (the most common saturated mid-tone, inside `rows` = (v0, height) if given), used for the team colour."""
    a = np.asarray(img.convert("RGB"), dtype=float) / 255.0
    mx, mn = a.max(-1), a.min(-1)
    lum = a.mean(-1)
    sat = (mx - mn) / np.maximum(mx, 1e-3)
    cand = (lum > 0.18) & (lum < 0.85) & (sat > 0.15)
    if rows:
        keep = np.zeros_like(cand)
        y0, y1 = int(rows[0] * a.shape[0]), int((rows[0] + rows[1]) * a.shape[0])
        keep[y0:y1] = True
        cand &= keep
    out = np.zeros(a.shape[:2] + (3,), dtype=np.uint8)
    if cand.sum() > 16:
        q = (a[cand] * 8).astype(int)
        keys = q[:, 0] * 64 + q[:, 1] * 8 + q[:, 2]
        top = np.bincount(keys).argmax()
        ref = np.array([top // 64, (top // 8) % 8, top % 8]) / 8.0 + 1 / 16
        d = np.linalg.norm(a - ref, axis=-1)
        near = d < 0.2
        if rows:
            near &= keep
        out[..., 0] = np.where(near, 165, 0).astype(np.uint8)
    m = Image.fromarray(out)
    return m.resize((m.width * k, m.height * k), Image.NEAREST)


def png(img):
    b = io.BytesIO()
    img.save(b, "PNG", optimize=True)
    return b.getvalue()


def atlas(data, root, cfg):
    """Packs every material's image (or flat colour) into one texture; returns (image, (v0, height) per material)."""
    if cfg.get("texture"):
        return Image.open(find(root, cfg["texture"])).convert("RGB"), [(0.0, 1.0)] * len(data["materials"])
    tiles, keys = [], []
    for m in data["materials"]:
        path = find(root, m["image"]) if m["image"] else None
        key = path or str(srgb(m["color"]))
        if key not in keys:
            keys.append(key)
            tile = Image.open(path).convert("RGB") if path else Image.new("RGB", (8, 8), srgb(m["color"]))
            if tile.width > 256:
                tile = tile.resize((256, max(1, tile.height * 256 // tile.width)), Image.LANCZOS)
            tiles.append(tile)
    width = max(t.width for t in tiles)
    tiles = [t if t.width == width else t.resize((width, max(1, t.height * width // t.width)), Image.NEAREST) for t in tiles]
    height = sum(t.height for t in tiles)
    img = Image.new("RGB", (width, height))
    rects, y = [], 0
    for t in tiles:
        img.paste(t, (0, y))
        rects.append((y / height, t.height / height))
        y += t.height
    remap = []
    for m in data["materials"]:
        path = find(root, m["image"]) if m["image"] else None
        remap.append(rects[keys.index(path or str(srgb(m["color"])))])
    return img, remap


def build(name, cfg):
    data, root = dump(name, cfg)
    objects = data["objects"]
    conv, along_x, scale, centre = normalise(objects, cfg["length"])
    flip = cfg.get("flip", True)
    part_of = {}
    for o in objects:
        part_of[o["name"]] = "hull"
        for part, keys in cfg.get("parts", {}).items():
            if any(k in o["name"] for k in keys):
                part_of[o["name"]] = part

    tris = {"hull": [], "turret": []}
    for o in objects:
        for t, mi in zip(o["tris"], o["mats"]):
            vs = [transform(v, conv, along_x, scale, centre, flip) for v in t]
            tris[part_of[o["name"]]].append(([p for p, _ in vs], [(v[6], v[7]) for v in t], mi))

    palette = cfg.get("palette", False)
    if palette:
        mats = []
        for m in data["materials"]:
            target = next((v for k, v in cfg.get("colors", {}).items() if k.lower() in m["name"].lower()), None)
            mats.append(gm.MAT[target or nearest_material(srgb(m["color"]))])
        images = [("color", "palette.png"), ("mask", "palette_mask.png")]
    else:
        img, remap = atlas(data, root, cfg)
        seed = sum(ord(c) for c in name)
        color, k = weather(img, cfg.get("wear", 0.5), seed)
        rows = None
        if cfg.get("team_material"):
            rows = remap[next(i for i, m in enumerate(data["materials"]) if m["name"] == cfg["team_material"])]
        mask = team_mask(img, k, rows) if cfg.get("team") else Image.new("RGB", color.size)
        if cfg.get("team"):
            c = np.asarray(color, dtype=float)
            m = np.asarray(mask, dtype=float)[..., :1] / 255.0
            grey = c.mean(-1, keepdims=True) * 1.25
            color = Image.fromarray(np.clip(c * (1 - m) + grey * m, 0, 255).astype(np.uint8))
        images = [("color", png(color)), ("mask", png(mask))]

    def mesh_of(items, origin):
        m = gm.Mesh()
        for pts, uvs, mi in items:
            pts = [tuple(float(x) for x in (p - origin)) for p in pts]
            if palette:
                m.face(pts, mats[mi])
                continue
            a, b, c = (np.array(p) for p in pts)
            n = np.cross(b - a, c - a)
            ln = np.linalg.norm(n)
            if ln < 1e-12:
                continue
            n = tuple(float(x) for x in n / ln)
            y0, hgt = remap[mi]
            base = len(m.pos)
            for p, (u, v) in zip(pts, uvs):
                m.pos.append(p)
                m.nrm.append(n)
                m.uv.append((u % 1.0 if u > 1.0 or u < 0.0 else u, y0 + (1.0 - min(max(v, 0.0), 1.0)) * hgt))
            m.idx += [base, base + 1, base + 2]
        return m

    parts, points = {}, {}
    zero = np.zeros(3)
    if tris["turret"]:
        tp = np.array([p for pts, _, _ in tris["turret"] for p in pts])
        tlo, thi = tp.min(0), tp.max(0)
        cut = cfg.get("barrel_cut", 0.0)
        front = [bool(cut) and max(p[2] for p in t[0]) < tlo[2] + (thi[2] - tlo[2]) * cut for t in tris["turret"]]
        body = [t for t, f in zip(tris["turret"], front) if not f]
        barrel = [t for t, f in zip(tris["turret"], front) if f]
        bp = np.array([p for pts, _, _ in body for p in pts]) if body else tp
        pivot = np.array([(bp[:, 0].min() + bp[:, 0].max()) / 2, bp[:, 1].min(), (bp[:, 2].min() + bp[:, 2].max()) / 2])
        parts["hull"] = (mesh_of(tris["hull"], zero), (0, 0, 0))
        parts["turret"] = (mesh_of(body, pivot), tuple(pivot))
        points["pivot"] = (tuple(float(x) for x in pivot), None)
        if barrel:
            parts["barrel"] = (mesh_of(barrel, pivot), tuple(pivot))
            bb = np.array([p for pts, _, _ in barrel for p in pts]) - pivot
            tip = (float((bb[:, 0].min() + bb[:, 0].max()) / 2), float((bb[:, 1].min() + bb[:, 1].max()) / 2), float(bb[:, 2].min()))
            points["muzzle"] = (tip, "barrel")
    else:
        parts["hull"] = (mesh_of(tris["hull"], zero), (0, 0, 0))
        if "pivot_at" in cfg:
            hp = np.array([p for pts, _, _ in tris["hull"] for p in pts])
            zlo, zhi = hp[:, 2].min(), hp[:, 2].max()
            z = zlo + (zhi - zlo) * cfg["pivot_at"]
            top = max(max(p[1] for p in pts) for pts, _, _ in tris["hull"] if min(p[2] for p in pts) <= z <= max(p[2] for p in pts))
            points["pivot"] = ((0.0, float(top), float(z)), None)
    return parts, points, images


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "data", "models")
    every = {**MODELS, **PROPS}
    only = sys.argv[2:] or list(every)
    for name in only:
        parts, points, images = build(name, every[name])
        gm.glb(os.path.join(out, name + ".glb"), parts, points, images)
        tri = sum(len(m.idx) // 3 for m, _ in parts.values())
        print(f"{name}: {tri} tris, parts {list(parts)}")


if __name__ == "__main__":
    main()
