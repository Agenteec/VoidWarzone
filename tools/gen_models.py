"""Procedural low-poly models for Void Warzone (glb, forward = -z, up = +y).

Usage: python tools/gen_models.py data/models
Each model has nodes `hull`, optional `turret` and `barrel` (both local to the turret pivot),
an empty `pivot` (turret rotation centre in hull space) and an empty `muzzle` (child of `barrel`).
Colours come from a shared palette texture; the mask texture holds team colour (R) and glow (G).
"""
import io
import json
import math
import os
import random
import struct
import sys

from PIL import Image, ImageDraw

CELL = 64
GRID = 8
ROWS = 4

# name: (rgb, team, glow)
MATERIALS = [
    ("graphite_dark", (44, 46, 52), 0, 0),
    ("graphite", (78, 82, 88), 0, 0),
    ("steel", (150, 155, 158), 0, 0),
    ("hazard", (236, 178, 40), 0, 0),
    ("team", (205, 205, 205), 255, 0),
    ("rubber", (26, 26, 28), 0, 0),
    ("glass", (70, 175, 225), 0, 150),
    ("warn", (255, 128, 40), 0, 255),
    ("void_shell", (30, 16, 38), 0, 0),
    ("void_purple", (92, 42, 126), 0, 0),
    ("void_crystal", (226, 96, 255), 255, 255),
    ("void_vein", (255, 64, 150), 0, 255),
    ("concrete", (128, 123, 114), 0, 0),
    ("concrete_dark", (86, 83, 78), 0, 0),
    ("team_dark", (120, 120, 120), 255, 0),
    ("energy", (95, 205, 255), 0, 255),
    ("rock", (104, 94, 82), 0, 0),
    ("rock_dark", (64, 58, 52), 0, 0),
    ("charred", (38, 32, 28), 0, 0),
    ("rust", (120, 62, 36), 0, 0),
    ("bark", (58, 46, 38), 0, 0),
    ("ember", (255, 110, 30), 0, 200),
    ("moss", (70, 74, 52), 0, 0),
    ("bone", (176, 168, 150), 0, 0),
    ("snow", (232, 237, 244), 0, 0),
    ("birch", (214, 210, 200), 0, 0),
    ("fir", (38, 62, 48), 0, 0),
    ("fir_dark", (24, 42, 34), 0, 0),
    ("paint_old", (92, 112, 104), 0, 0),
    ("lamp", (255, 200, 120), 0, 255),
    ("wood", (104, 80, 58), 0, 0),
]
MAT = {m[0]: i for i, m in enumerate(MATERIALS)}


def palette():
    rnd = random.Random(7)
    color = Image.new("RGBA", (CELL * GRID, CELL * ROWS))
    mask = Image.new("RGBA", (CELL * GRID, CELL * ROWS), (0, 0, 0, 255))
    px = color.load()
    for i, (_, rgb, team, glow) in enumerate(MATERIALS):
        cx, cy = (i % GRID) * CELL, (i // GRID) * CELL
        for y in range(CELL):
            for x in range(CELL):
                edge = min(x, y, CELL - 1 - x, CELL - 1 - y)
                k = 1.0 + rnd.uniform(-0.05, 0.05)
                if edge < 3:
                    k *= 0.62
                elif edge < 6:
                    k *= 1.12
                if glow and edge >= 6:
                    k = 1.0 + rnd.uniform(-0.02, 0.02)
                px[cx + x, cy + y] = tuple(max(0, min(255, int(c * k))) for c in rgb) + (255,)
        ImageDraw.Draw(mask).rectangle([cx, cy, cx + CELL - 1, cy + CELL - 1], fill=(team, glow, 0, 255))
    def png(img):
        b = io.BytesIO()
        img.save(b, "PNG", optimize=True)
        return b.getvalue()
    return png(color), png(mask)


class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def face(self, pts, mat):
        a, b, c = pts[0], pts[1], pts[2]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        ln = math.sqrt(sum(x * x for x in n)) or 1.0
        n = [x / ln for x in n]
        # planar uv inside the material cell
        ax = max(range(3), key=lambda i: abs(n[i]))
        pa, pb = [(1, 2), (0, 2), (0, 1)][ax]
        us = [p[pa] for p in pts]
        vs = [p[pb] for p in pts]
        u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
        cx, cy = (mat % GRID) * CELL, (mat // GRID) * CELL
        base = len(self.pos)
        for p in pts:
            fu = (p[pa] - u0) / (u1 - u0) if u1 > u0 else 0.5
            fv = (p[pb] - v0) / (v1 - v0) if v1 > v0 else 0.5
            self.pos.append(tuple(p))
            self.nrm.append(tuple(n))
            self.uv.append(((cx + 1 + fu * (CELL - 2)) / (CELL * GRID), (cy + 1 + fv * (CELL - 2)) / (CELL * ROWS)))
        for i in range(1, len(pts) - 1):
            self.idx += [base, base + i, base + i + 1]

    def box(self, c, s, mat, top=(1.0, 1.0), shift=(0.0, 0.0), sides=None, bottom=True):
        """Box centred at c with size s; `top` scales the top face (taper), `shift` moves it (slopes)."""
        hx, hy, hz = s[0] / 2, s[1] / 2, s[2] / 2
        tx, tz = hx * top[0], hz * top[1]
        lo = [(c[0] - hx, c[1] - hy, c[2] - hz), (c[0] + hx, c[1] - hy, c[2] - hz), (c[0] + hx, c[1] - hy, c[2] + hz), (c[0] - hx, c[1] - hy, c[2] + hz)]
        ox, oz = c[0] + shift[0], c[2] + shift[1]
        hi = [(ox - tx, c[1] + hy, oz - tz), (ox + tx, c[1] + hy, oz - tz), (ox + tx, c[1] + hy, oz + tz), (ox - tx, c[1] + hy, oz + tz)]
        m = sides or {}
        self.face([hi[0], hi[3], hi[2], hi[1]], m.get("top", mat))
        if bottom:
            self.face([lo[0], lo[1], lo[2], lo[3]], m.get("bottom", mat))
        self.face([lo[0], hi[0], hi[1], lo[1]], m.get("front", mat))
        self.face([lo[2], hi[2], hi[3], lo[3]], m.get("back", mat))
        self.face([lo[1], hi[1], hi[2], lo[2]], m.get("right", mat))
        self.face([lo[3], hi[3], hi[0], lo[0]], m.get("left", mat))

    def cyl(self, c, r, h, mat, n=10, axis="y", r2=None, cap=True, capmat=None):
        r2 = r if r2 is None else r2
        def P(a, t, rad):
            x, y = math.cos(a) * rad, math.sin(a) * rad
            if axis == "y":
                return (c[0] + x, c[1] + t, c[2] + y)
            if axis == "z":
                return (c[0] + x, c[1] + y, c[2] + t)
            return (c[0] + t, c[1] + y, c[2] + x)
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1) / n
            q = [P(a0, -h / 2, r), P(a1, -h / 2, r), P(a1, h / 2, r2), P(a0, h / 2, r2)]
            self.face(q if axis != "y" else q[::-1], mat)
        if cap:
            top = [P(2 * math.pi * i / n, h / 2, r2) for i in range(n)]
            bot = [P(2 * math.pi * i / n, -h / 2, r) for i in range(n)]
            self.face(top if axis != "y" else top[::-1], capmat if capmat is not None else mat)
            self.face(bot[::-1] if axis != "y" else bot, capmat if capmat is not None else mat)

    def strut(self, a, b, t, mat):
        d = [b[i] - a[i] for i in range(3)]
        ln = math.sqrt(sum(x * x for x in d)) or 1.0
        d = [x / ln for x in d]
        up = [0.0, 1.0, 0.0] if abs(d[1]) < 0.9 else [1.0, 0.0, 0.0]
        def cross(p, q):
            return [p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0]]
        u = cross(d, up)
        lu = math.sqrt(sum(x * x for x in u))
        u = [x / lu * t for x in u]
        v = cross(d, u)
        lv = math.sqrt(sum(x * x for x in v))
        v = [x / lv * t for x in v]
        def ring(c, k):
            return [tuple(c[i] + (su * u[i] + sv * v[i]) * k for i in range(3)) for su, sv in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
        ra, rb = ring(a, 1.0), ring(b, 0.7)
        for i in range(4):
            j = (i + 1) % 4
            self.face([ra[i], rb[i], rb[j], ra[j]], mat)
        self.face(ra, mat)
        self.face(rb[::-1], mat)

    def crystal(self, c, r, h, mat, n=5, tilt=(0.0, 0.0), base=None):
        tip = (c[0] + tilt[0], c[1] + h, c[2] + tilt[1])
        ring = [(c[0] + math.cos(2 * math.pi * i / n) * r, c[1] + h * 0.25, c[2] + math.sin(2 * math.pi * i / n) * r) for i in range(n)]
        low = [(c[0] + math.cos(2 * math.pi * i / n) * r * 0.6, c[1], c[2] + math.sin(2 * math.pi * i / n) * r * 0.6) for i in range(n)]
        for i in range(n):
            j = (i + 1) % n
            self.face([ring[i], tip, ring[j]][::-1], mat)
            self.face([low[i], ring[i], ring[j], low[j]][::-1], base if base is not None else mat)
        return tip


def glb(path, parts, points, images):
    """parts: {name: (Mesh, translation)}; points: {name: (translation, parent or None)}."""
    bin_ = bytearray()
    views, accessors, meshes, nodes = [], [], [], []

    def add_view(data, target=None):
        while len(bin_) % 4:
            bin_.append(0)
        off = len(bin_)
        bin_.extend(data)
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v["target"] = target
        views.append(v)
        return len(views) - 1

    def add_acc(values, comp, typ, minmax=False, target=34962):
        flat = [x for v in values for x in (v if isinstance(v, tuple) else (v,))]
        fmt = "<" + ("f" if comp == 5126 else "I") * len(flat)
        view = add_view(struct.pack(fmt, *flat), target)
        a = {"bufferView": view, "componentType": comp, "count": len(values), "type": typ}
        if minmax:
            a["min"] = [min(v[i] for v in values) for i in range(3)]
            a["max"] = [max(v[i] for v in values) for i in range(3)]
        accessors.append(a)
        return len(accessors) - 1

    name_to_node = {}
    for name, (mesh, tr) in parts.items():
        if not mesh.idx:
            continue
        p = add_acc(mesh.pos, 5126, "VEC3", True)
        n = add_acc(mesh.nrm, 5126, "VEC3")
        t = add_acc(mesh.uv, 5126, "VEC2")
        i = add_acc(mesh.idx, 5125, "SCALAR", target=34963)
        meshes.append({"name": name, "primitives": [{"attributes": {"POSITION": p, "NORMAL": n, "TEXCOORD_0": t}, "indices": i, "material": 0}]})
        nodes.append({"name": name, "mesh": len(meshes) - 1, "translation": list(tr)})
        name_to_node[name] = len(nodes) - 1
    roots = list(range(len(nodes)))
    for name, (tr, parent) in points.items():
        nodes.append({"name": name, "translation": list(tr)})
        idx = len(nodes) - 1
        if parent and parent in name_to_node:
            nodes[name_to_node[parent]].setdefault("children", []).append(idx)
        else:
            roots.append(idx)
    imgs = [{"name": nm, "uri": uri} for nm, uri in images]
    doc = {
        "asset": {"version": "2.0", "generator": "void-warzone gen_models.py"},
        "scene": 0,
        "scenes": [{"nodes": roots}],
        "nodes": nodes,
        "meshes": meshes,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(bin_)}],
        "images": imgs,
        "samplers": [{"magFilter": 9729, "minFilter": 9987}],
        "textures": [{"source": 0, "sampler": 0}],
        "materials": [{"name": "palette", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0}}],
    }
    js = json.dumps(doc, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    while len(bin_) % 4:
        bin_.append(0)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js
    out += struct.pack("<II", len(bin_), 0x004E4942) + bytes(bin_)
    with open(path, "wb") as f:
        f.write(out)


# ---------------------------------------------------------------- colony vehicles

def tracked_hull(m, wide=1.0, heavy=False):
    w = 0.9 * wide
    for sx in (-1, 1):
        m.box((sx * w, 0.32, 0.0), (0.46, 0.56, 2.5), MAT["rubber"], top=(1.0, 0.9))
        for z in (-0.85, -0.3, 0.25, 0.8):
            m.cyl((sx * (w + 0.235), 0.3, z), 0.2, 0.03, MAT["graphite"], n=8, axis="x")
        m.box((sx * w, 0.66, 0.0), (0.56, 0.12, 2.6), MAT["graphite_dark"])
        m.box((sx * w, 0.735, 0.55), (0.5, 0.03, 0.7), MAT["team_dark"])
    h = 0.62 if heavy else 0.5
    m.box((0.0, 0.66 + h / 2, 0.15), (1.36 * wide, h, 1.9), MAT["graphite"], top=(0.92, 0.8), shift=(0.0, 0.12),
          sides={"front": MAT["hazard"]})
    m.box((0.0, 0.62, -1.05), (1.2 * wide, 0.34, 0.5), MAT["graphite_dark"], top=(1.0, 0.4), shift=(0.0, 0.1))
    m.box((0.0, 0.66 + h + 0.03, 0.85), (0.9 * wide, 0.06, 0.4), MAT["steel"])
    m.cyl((0.45 * wide, 0.66 + h + 0.12, 0.95), 0.06, 0.24, MAT["warn"], n=6)
    return (0.0, 0.66 + h, 0.05)


def turret_base(m, size=1.0, mat="graphite"):
    m.box((0.0, 0.2 * size, 0.08), (0.95 * size, 0.4 * size, 1.05 * size), MAT[mat], top=(0.82, 0.8), shift=(0.0, 0.05),
          sides={"left": MAT["team"], "right": MAT["team"]})
    m.box((0.0, 0.42 * size, 0.25), (0.5 * size, 0.06 * size, 0.4 * size), MAT["graphite_dark"])


def cannon(m, r=0.09, length=1.3, y=0.2, twin=0.0, brake=True):
    xs = [0.0] if twin == 0.0 else [-twin, twin]
    for x in xs:
        m.cyl((x, y, -0.3 - length / 2), r, length, MAT["steel"], n=8, axis="z")
        if brake:
            m.box((x, y, -0.3 - length), (r * 2.6, r * 2.2, r * 2.4), MAT["graphite_dark"])
    m.box((0.0, y, -0.32), (max(0.3, twin * 2 + 0.3), 0.26, 0.24), MAT["graphite_dark"])
    return (0.0, y, -0.3 - length - 0.1)


def rocket_pod(m, rows=2, cols=3, y=0.3, size=0.16, length=0.9):
    w = cols * size * 2.1
    h = rows * size * 2.1
    m.box((0.0, y, -0.2), (w + 0.12, h + 0.12, length), MAT["graphite"], sides={"front": MAT["graphite_dark"], "left": MAT["team"], "right": MAT["team"]})
    for r_ in range(rows):
        for c_ in range(cols):
            x = -w / 2 + size * 1.05 + c_ * size * 2.1
            yy = y - h / 2 + size * 1.05 + r_ * size * 2.1
            m.cyl((x, yy, -0.2 - length / 2 - 0.02), size * 0.8, 0.06, MAT["warn"] if (r_ + c_) % 2 else MAT["hazard"], n=6, axis="z")
    return (0.0, y, -0.2 - length / 2 - 0.1)


def dish(m, c, r, mat="steel"):
    m.cyl(c, 0.05, 0.5, MAT["graphite_dark"], n=6)
    m.cyl((c[0], c[1] + 0.3, c[2]), r, 0.08, MAT[mat], n=10, r2=r * 0.4)
    m.cyl((c[0], c[1] + 0.38, c[2]), 0.05, 0.1, MAT["glass"], n=6)


def antenna(m, x, z, h):
    m.cyl((x, h / 2, z), 0.025, h, MAT["graphite_dark"], n=5)
    m.cyl((x, h, z), 0.05, 0.06, MAT["warn"], n=6)


def tank(variant):
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    heavy = variant in ("assault", "siege", "warlord")
    pivot = tracked_hull(hull, 1.12 if variant == "siege" else 1.0, heavy)
    muzzle = (0.0, 0.2, -1.6)
    if variant == "base":
        turret_base(tur)
        muzzle = cannon(bar)
    elif variant == "assault":
        turret_base(tur, 1.15)
        muzzle = cannon(bar, r=0.13, length=1.45, y=0.24)
    elif variant == "siege":
        turret_base(tur, 1.3, "graphite_dark")
        muzzle = cannon(bar, r=0.12, length=1.6, y=0.28, twin=0.16)
    elif variant == "rocket":
        tur.box((0.0, 0.12, 0.2), (0.8, 0.24, 0.8), MAT["graphite"])
        muzzle = rocket_pod(bar)
    elif variant == "artillery":
        turret_base(tur, 1.1)
        muzzle = cannon(bar, r=0.1, length=2.2, y=0.26)
    elif variant == "missile":
        tur.box((0.0, 0.12, 0.2), (0.8, 0.24, 0.8), MAT["graphite"])
        muzzle = rocket_pod(bar, rows=2, cols=2, size=0.22, length=1.2)
        dish(tur, (0.32, 0.25, 0.4), 0.18)
    elif variant in ("radar", "sensor"):
        turret_base(tur, 0.9)
        dish(tur, (0.0, 0.42, 0.3), 0.42 if variant == "radar" else 0.6, "steel" if variant == "radar" else "team")
        if variant == "sensor":
            antenna(tur, -0.35, 0.45, 1.3)
            antenna(tur, 0.35, 0.45, 1.1)
        muzzle = cannon(bar, r=0.05, length=0.7, y=0.18, brake=False)
    elif variant in ("command", "warlord"):
        turret_base(tur, 1.1 if variant == "warlord" else 1.0)
        antenna(tur, -0.38, 0.4, 1.4)
        antenna(tur, 0.38, 0.4, 1.0)
        tur.box((-0.38, 1.25, 0.32), (0.02, 0.22, 0.32), MAT["team"])
        muzzle = cannon(bar, r=0.09, length=1.25, y=0.22, twin=0.13 if variant == "warlord" else 0.0)
    elif variant in ("repair", "mechanic"):
        turret_base(tur, 0.9)
        for sx in ((-1, 1) if variant == "mechanic" else (1,)):
            tur.box((sx * 0.35, 0.55, 0.0), (0.12, 0.6, 0.12), MAT["hazard"], top=(1, 1), shift=(0.0, -0.25))
            tur.box((sx * 0.35, 0.85, -0.45), (0.1, 0.1, 0.5), MAT["hazard"])
            tur.box((sx * 0.35, 0.78, -0.72), (0.18, 0.16, 0.12), MAT["energy"])
        muzzle = cannon(bar, r=0.05, length=0.6, y=0.15, brake=False)
    return {"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)}, {"pivot": (pivot, None), "muzzle": (muzzle, "barrel")}


def wheeled_hull(m, long=1.0):
    for sx in (-1, 1):
        for z in (-0.6 * long, 0.65 * long):
            m.cyl((sx * 0.62, 0.3, z), 0.3, 0.26, MAT["rubber"], n=10, axis="x", capmat=MAT["graphite"])
    m.box((0.0, 0.52, 0.05), (1.0, 0.3, 1.9 * long), MAT["graphite"], top=(0.95, 0.85), sides={"front": MAT["hazard"]})
    m.box((0.0, 0.78, -0.25), (0.7, 0.26, 0.6), MAT["glass"], top=(0.8, 0.5), shift=(0.0, 0.1))
    for sx in (-1, 1):
        m.box((sx * 0.52, 0.6, 0.05), (0.04, 0.16, 1.4 * long), MAT["team"])
    return (0.0, 0.7, 0.45)


def scout(variant):
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    pivot = wheeled_hull(hull, 1.1 if variant == "hunter" else 1.0)
    tur.box((0.0, 0.1, 0.05), (0.4, 0.2, 0.45), MAT["graphite_dark"])
    if variant in ("emp", "jammer"):
        bar.cyl((0.0, 0.25, -0.35), 0.18 if variant == "emp" else 0.26, 0.12, MAT["energy"], n=10, axis="z", r2=0.08)
        bar.box((0.0, 0.25, -0.1), (0.12, 0.12, 0.4), MAT["steel"])
        muzzle = (0.0, 0.25, -0.5)
    else:
        twin = 0.07 if variant in ("raider", "hunter") else 0.0
        muzzle = cannon(bar, r=0.035, length=0.6, y=0.18, twin=twin, brake=False)
    if variant in ("raider", "hunter"):
        hull.box((0.0, 0.8, 0.95), (0.9, 0.05, 0.2), MAT["hazard"])
        hull.box((0.0, 0.7, 0.95), (0.06, 0.2, 0.06), MAT["graphite_dark"])
    return {"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)}, {"pivot": (pivot, None), "muzzle": (muzzle, "barrel")}


def engineer():
    hull = Mesh()
    for sx in (-1, 1):
        for z in (-0.7, 0.1, 0.75):
            hull.cyl((sx * 0.6, 0.27, z), 0.27, 0.24, MAT["rubber"], n=10, axis="x", capmat=MAT["graphite"])
    hull.box((0.0, 0.5, 0.1), (1.0, 0.22, 2.0), MAT["graphite"])
    hull.box((0.0, 0.85, -0.6), (0.95, 0.5, 0.65), MAT["hazard"], top=(0.9, 0.7), shift=(0.0, 0.08), sides={"front": MAT["glass"]})
    hull.box((0.0, 0.66, 0.5), (0.9, 0.1, 1.0), MAT["steel"])
    hull.box((0.3, 1.0, 0.45), (0.12, 0.7, 0.12), MAT["hazard"], top=(1, 1), shift=(0.0, -0.2))
    hull.box((0.3, 1.32, 0.05), (0.1, 0.1, 0.8), MAT["hazard"])
    hull.box((0.3, 1.2, -0.32), (0.04, 0.24, 0.04), MAT["graphite_dark"])
    hull.box((-0.25, 0.85, 0.7), (0.35, 0.3, 0.35), MAT["team"])
    hull.cyl((-0.35, 1.18, -0.6), 0.06, 0.1, MAT["warn"], n=6)
    return {"hull": (hull, (0, 0, 0))}, {"pivot": ((0.0, 1.0, 0.0), None)}


def rotor(m, c, r, angle):
    for k in range(2):
        a = angle + k * math.pi / 2
        dx, dz = math.cos(a) * r, math.sin(a) * r
        m.strut((c[0] - dx, c[1], c[2] - dz), (c[0] + dx, c[1], c[2] + dz), 0.035, MAT["steel"])
    m.cyl(c, 0.08, 0.08, MAT["graphite_dark"], n=6)


def gunship(variant):
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    hull.box((0.0, 0.6, 0.0), (0.7, 0.55, 2.2), MAT["graphite"], top=(0.8, 0.85), sides={"front": MAT["glass"]})
    hull.box((0.0, 0.55, -1.25), (0.5, 0.4, 0.4), MAT["glass"], top=(0.6, 0.4), shift=(0.0, 0.05))
    hull.box((0.0, 0.75, 1.55), (0.22, 0.25, 1.1), MAT["graphite_dark"], top=(0.8, 1.0))
    hull.box((0.0, 1.0, 2.0), (0.06, 0.5, 0.35), MAT["team"])
    for sx in (-1, 1):
        hull.box((sx * 0.75, 0.62, 0.0), (0.8, 0.08, 0.35), MAT["graphite_dark"])
        hull.cyl((sx * 1.15, 0.65, 0.0), 0.32, 0.3, MAT["graphite"], n=10, capmat=MAT["graphite_dark"])
        rotor(hull, (sx * 1.15, 0.84, 0.0), 0.62, 0.5 + sx * 0.3)
        hull.box((sx * 0.4, 0.6, 0.3), (0.04, 0.2, 1.0), MAT["team"])
    tur.box((0.0, -0.05, 0.0), (0.3, 0.18, 0.3), MAT["graphite_dark"])
    if variant == "rocket":
        muzzle = rocket_pod(bar, rows=1, cols=2, y=-0.05, size=0.12, length=0.6)
    else:
        muzzle = cannon(bar, r=0.04, length=0.6, y=-0.05, twin=0.06 if variant == "interceptor" else 0.0, brake=False)
    pivot = (0.0, 0.32, -0.75)
    return {"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)}, {"pivot": (pivot, None), "muzzle": (muzzle, "barrel")}


def air_transport():
    hull = Mesh()
    hull.box((0.0, 0.8, 0.0), (1.4, 1.0, 3.6), MAT["graphite"], top=(0.85, 0.92), sides={"front": MAT["hazard"]})
    hull.box((0.0, 0.75, -2.05), (1.1, 0.7, 0.6), MAT["glass"], top=(0.7, 0.4), shift=(0.0, 0.1))
    hull.box((0.0, 1.0, 2.2), (0.6, 0.5, 1.0), MAT["graphite_dark"], top=(0.7, 1.0))
    for sx in (-1, 1):
        hull.box((sx * 0.72, 0.8, 0.0), (0.04, 0.4, 2.6), MAT["team"])
        for z in (-1.1, 1.1):
            hull.box((sx * 1.0, 1.2, z), (0.7, 0.1, 0.3), MAT["graphite_dark"])
            hull.cyl((sx * 1.45, 1.15, z), 0.3, 0.32, MAT["graphite"], n=10, capmat=MAT["graphite_dark"])
            rotor(hull, (sx * 1.45, 1.36, z), 0.7, z + sx)
        hull.cyl((sx * 0.45, 0.25, 1.5), 0.08, 0.1, MAT["warn"], n=6)
    return {"hull": (hull, (0, 0, 0))}, {"pivot": ((0.0, 1.3, 0.0), None)}


# ---------------------------------------------------------------- colony structures

def core():
    m = Mesh()
    m.cyl((0, 0.35, 0), 2.6, 0.7, MAT["concrete_dark"], n=8, r2=2.4, capmat=MAT["concrete"])
    m.cyl((0, 1.3, 0), 1.9, 1.2, MAT["graphite"], n=8, r2=1.6)
    m.cyl((0, 2.0, 0), 1.65, 0.18, MAT["hazard"], n=8)
    m.cyl((0, 2.6, 0), 1.2, 1.0, MAT["graphite_dark"], n=8, r2=0.9)
    m.cyl((0, 3.4, 0), 0.55, 0.7, MAT["energy"], n=8, r2=0.35)
    for i in range(8):
        a = 2 * math.pi * (i + 0.5) / 8
        m.box((math.cos(a) * 1.85, 1.3, math.sin(a) * 1.85), (0.5, 0.9, 0.5), MAT["team"])
        m.box((math.cos(a) * 2.45, 0.85, math.sin(a) * 2.45), (0.25, 0.3, 0.25), MAT["warn"] if i % 2 else MAT["hazard"])
    return {"hull": (m, (0, 0, 0))}, {"pivot": ((0, 3.0, 0), None)}


def tower_base(m, h=2.6, w=1.1):
    m.box((0, 0.25, 0), (w * 1.6, 0.5, w * 1.6), MAT["concrete_dark"], top=(0.9, 0.9))
    m.box((0, 0.5 + h / 2, 0), (w, h, w), MAT["concrete"], top=(0.8, 0.8), sides={"front": MAT["graphite"]})
    m.box((0, 0.5 + h * 0.6, 0), (w * 0.95, 0.15, w * 0.95), MAT["hazard"])
    m.box((0, 0.5 + h * 0.35, -w * 0.45), (w * 0.5, 0.4, 0.06), MAT["team"])
    return (0, 0.5 + h, 0)


def mg_tower():
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    pivot = tower_base(hull)
    tur.box((0, 0.2, 0.05), (0.8, 0.4, 0.8), MAT["graphite"], top=(0.85, 0.85), sides={"left": MAT["team"], "right": MAT["team"]})
    muzzle = cannon(bar, r=0.05, length=0.8, y=0.22, twin=0.1, brake=False)
    return {"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)}, {"pivot": (pivot, None), "muzzle": (muzzle, "barrel")}


def bunker(heavy=False):
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    hull.box((0, 0.45, 0), (2.0, 0.9, 2.0), MAT["concrete"], top=(0.8, 0.8), sides={"front": MAT["concrete_dark"]})
    hull.box((0, 0.95, 0), (1.5, 0.1, 1.5), MAT["hazard"])
    for sx in (-1, 1):
        hull.box((sx * 0.98, 0.5, 0), (0.06, 0.4, 1.0), MAT["team"])
    pivot = (0, 1.0, 0)
    turret_base(tur, 1.2 if heavy else 1.0)
    muzzle = cannon(bar, r=0.12 if heavy else 0.1, length=1.5 if heavy else 1.3, y=0.25)
    return {"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)}, {"pivot": (pivot, None), "muzzle": (muzzle, "barrel")}


def wall():
    m = Mesh()
    m.box((0, 0.6, 0), (1.0, 1.2, 0.8), MAT["concrete"], top=(1.0, 0.7), sides={"top": MAT["concrete_dark"]})
    m.box((0, 0.95, -0.3), (1.0, 0.12, 0.05), MAT["hazard"])
    m.box((0, 0.95, 0.3), (1.0, 0.12, 0.05), MAT["hazard"])
    return {"hull": (m, (0, 0, 0))}, {}


def factory():
    m = Mesh()
    m.box((0, 0.9, 0.2), (3.6, 1.8, 3.0), MAT["graphite"], top=(1.0, 0.85), sides={"top": MAT["graphite_dark"]})
    m.box((0, 0.7, -1.4), (1.8, 1.4, 0.3), MAT["hazard"], sides={"front": MAT["graphite_dark"]})
    m.box((0, 0.65, -1.56), (1.4, 1.2, 0.04), MAT["rubber"])
    for sx in (-1, 1):
        m.box((sx * 1.82, 1.0, 0.2), (0.06, 0.6, 2.2), MAT["team"])
    m.cyl((1.2, 2.3, 1.1), 0.25, 1.4, MAT["steel"], n=8)
    m.cyl((1.2, 3.05, 1.1), 0.3, 0.1, MAT["warn"], n=8)
    m.box((-0.8, 2.0, 0.8), (1.2, 0.4, 1.0), MAT["graphite_dark"])
    m.box((-0.8, 2.25, 0.8), (0.9, 0.1, 0.7), MAT["energy"])
    return {"hull": (m, (0, 0, 0))}, {}


def generator():
    m = Mesh()
    m.box((0, 0.3, 0), (2.8, 0.6, 2.8), MAT["concrete_dark"], top=(0.95, 0.95))
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.cyl((sx * 0.75, 1.1, sz * 0.75), 0.45, 1.2, MAT["steel"], n=10, capmat=MAT["graphite"])
            m.cyl((sx * 0.75, 1.1, sz * 0.75), 0.47, 0.12, MAT["team"], n=10)
    m.cyl((0, 1.3, 0), 0.38, 1.6, MAT["energy"], n=8)
    m.box((0, 2.15, 0), (1.9, 0.12, 0.2), MAT["hazard"])
    m.box((0, 2.15, 0), (0.2, 0.12, 1.9), MAT["hazard"])
    return {"hull": (m, (0, 0, 0))}, {}


def lab():
    m = Mesh()
    m.box((0, 0.5, 0), (3.0, 1.0, 3.0), MAT["concrete"], top=(0.9, 0.9))
    m.cyl((0, 1.3, 0), 1.2, 0.8, MAT["glass"], n=10, r2=0.6)
    m.box((1.0, 1.1, 1.0), (0.5, 0.2, 0.5), MAT["team"])
    dish(m, (-1.0, 1.0, -1.0), 0.45)
    return {"hull": (m, (0, 0, 0))}, {}


# ---------------------------------------------------------------- the Void

def void_walker(size=1.0, crystals=3, legs=4, seed=1):
    rnd = random.Random(seed)
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    k = size
    y = 0.75 * k
    hull.cyl((0, y, 0.15 * k), 0.42 * k, 1.3 * k, MAT["void_shell"], n=6, axis="z", r2=0.26 * k)
    hull.cyl((0, y + 0.05 * k, -0.6 * k), 0.3 * k, 0.4 * k, MAT["void_purple"], n=6, axis="z", r2=0.18 * k)
    hull.box((0, y + 0.32 * k, 0.15 * k), (0.5 * k, 0.12 * k, 1.1 * k), MAT["void_purple"], top=(0.6, 0.85))
    parts = {"hull": (hull, (0, 0, 0))}
    points = {}
    for i in range(legs):
        sx = -1 if i % 2 else 1
        z = (-0.45 + (i // 2) * (0.9 / max(1, legs // 2 - 1))) * k
        hip = (sx * 0.3 * k, y, z)
        leg = Mesh()
        knee = (sx * 0.6 * k, 0.35 * k, z * 0.2)
        foot = (sx * 0.85 * k, -y, z * 0.35)
        leg.strut((0, 0, 0), knee, 0.07 * k, MAT["void_purple"])
        leg.strut(knee, foot, 0.06 * k, MAT["void_shell"])
        leg.cyl(knee, 0.08 * k, 0.12 * k, MAT["void_vein"], n=5)
        parts["leg%d" % i] = (leg, hip)
        points["hip%d" % i] = (hip, None)
    for i in range(crystals):
        tip = hull.crystal((rnd.uniform(-0.22, 0.22) * k, y + 0.3 * k, rnd.uniform(-0.1, 0.7) * k), 0.12 * k, rnd.uniform(0.35, 0.7) * k,
                           MAT["void_crystal"], tilt=(rnd.uniform(-0.15, 0.15) * k, rnd.uniform(0.0, 0.25) * k), base=MAT["void_vein"])
        points["tip%d" % i] = (tip, None)
    pivot = (0.0, y + 0.1 * k, -0.7 * k)
    tur.crystal((0, -0.1 * k, 0), 0.16 * k, 0.22 * k, MAT["void_purple"], n=6, base=MAT["void_shell"])
    bar.strut((0, 0.06 * k, -0.05 * k), (0, 0.06 * k, -0.6 * k), 0.07 * k, MAT["void_shell"])
    bar.crystal((0, 0.06 * k, -0.62 * k), 0.06 * k, 0.16 * k, MAT["void_vein"], n=4)
    parts["turret"] = (tur, pivot)
    parts["barrel"] = (bar, pivot)
    points["pivot"] = (pivot, None)
    points["muzzle"] = ((0, 0.06 * k, -0.75 * k), "barrel")
    return parts, points


def void_spire(h=3.0, seed=3, turret=True):
    rnd = random.Random(seed)
    hull, tur, bar = Mesh(), Mesh(), Mesh()
    hull.cyl((0, 0.25, 0), 1.2, 0.5, MAT["void_shell"], n=7, r2=1.0, capmat=MAT["void_purple"])
    tips = {}
    for i in range(7):
        a = 2 * math.pi * i / 7
        t = hull.crystal((math.cos(a) * 0.8, 0.3, math.sin(a) * 0.8), 0.18, rnd.uniform(0.5, 1.1), MAT["void_vein"], tilt=(math.cos(a) * 0.3, math.sin(a) * 0.3),
                         base=MAT["void_shell"])
        if i % 2 == 0:
            tips["tip%d" % len(tips)] = (t, None)
    tips["tip%d" % len(tips)] = (hull.crystal((0, 0.4, 0), 0.55, h, MAT["void_crystal"], n=6, base=MAT["void_purple"]), None)
    pivot = (0, 0.4 + h * 0.55, 0)
    if turret:
        tur.crystal((0, -0.2, 0), 0.35, 0.5, MAT["void_purple"], n=5)
        bar.box((0, 0.1, -0.4), (0.18, 0.18, 0.8), MAT["void_shell"], top=(0.5, 1.0))
        bar.crystal((0, 0.1, -0.8), 0.1, 0.25, MAT["void_vein"], n=4)
        return ({"hull": (hull, (0, 0, 0)), "turret": (tur, pivot), "barrel": (bar, pivot)},
                dict({"pivot": (pivot, None), "muzzle": ((0, 0.1, -0.95), "barrel")}, **tips))
    return {"hull": (hull, (0, 0, 0))}, dict({"pivot": (pivot, None)}, **tips)


def portal():
    m = Mesh()
    rnd = random.Random(9)
    m.cyl((0, 0.3, 0), 3.0, 0.6, MAT["void_shell"], n=9, r2=2.7, capmat=MAT["void_purple"])
    m.box((0, 0.75, 0), (4.6, 0.3, 1.6), MAT["void_purple"], top=(0.9, 0.8))
    cy, R = 3.2, 2.3
    seg = 14
    ring = []
    for i in range(seg + 1):
        a = math.pi * 2 * i / seg
        ring.append((math.cos(a) * R, cy + math.sin(a) * R, 0.0))
    for i in range(seg):
        m.strut(ring[i], ring[i + 1], 0.32 if i % 2 else 0.26, MAT["void_shell"] if i % 3 else MAT["void_purple"])
    for i in range(0, seg, 2):
        a = math.pi * 2 * (i + 0.5) / seg
        base = (math.cos(a) * (R + 0.2), cy + math.sin(a) * (R + 0.2), 0.0)
        out = (math.cos(a) * (R + 1.2 + rnd.uniform(0.0, 0.6)), cy + math.sin(a) * (R + 1.2 + rnd.uniform(0.0, 0.6)), rnd.uniform(-0.2, 0.2))
        m.strut(base, out, 0.16, MAT["void_crystal"])
    for sx in (-1, 1):
        m.strut((sx * 1.6, 0.8, 0.3), (sx * 2.2, cy - 0.6, 0.0), 0.35, MAT["void_shell"])
        m.crystal((sx * 2.6, 0.6, 0.4), 0.45, 2.6, MAT["void_crystal"], tilt=(sx * 0.6, 0.2), base=MAT["void_purple"])
        m.crystal((sx * 2.2, 0.6, -0.6), 0.3, 1.8, MAT["void_vein"], tilt=(sx * 0.4, -0.3), base=MAT["void_purple"])
    return {"hull": (m, (0, 0, 0))}, {"gate": ((0.0, cy, 0.0), None), "gate_edge": ((R - 0.15, cy, 0.0), None)}


def pylon():
    m = Mesh()
    m.cyl((0, 0.25, 0), 1.0, 0.5, MAT["void_shell"], n=6, r2=0.8, capmat=MAT["void_purple"])
    for i in range(3):
        a = 2 * math.pi * i / 3
        m.strut((math.cos(a) * 0.8, 0.4, math.sin(a) * 0.8), (math.cos(a) * 0.25, 2.2, math.sin(a) * 0.25), 0.12, MAT["void_purple"])
    tip = m.crystal((0, 1.6, 0), 0.4, 2.6, MAT["void_crystal"], n=6, base=MAT["void_vein"])
    m.crystal((0, 0.5, 0), 0.3, 1.0, MAT["void_vein"], n=5)
    return {"hull": (m, (0, 0, 0))}, {"tip0": (tip, None), "pivot": ((0, 3.6, 0), None)}


def void_hive(kind):
    m = Mesh()
    rnd = random.Random(len(kind))
    m.cyl((0, 0.5, 0), 2.4, 1.0, MAT["void_shell"], n=8, r2=1.9, capmat=MAT["void_purple"])
    for i in range(6):
        a = 2 * math.pi * i / 6 + 0.3
        m.crystal((math.cos(a) * 1.4, 0.9, math.sin(a) * 1.4), 0.3, rnd.uniform(0.8, 1.6), MAT["void_crystal" if i % 2 else "void_vein"],
                  tilt=(math.cos(a) * 0.4, math.sin(a) * 0.4), base=MAT["void_shell"])
    m.crystal((0, 1.0, 0), 0.8, 2.2 if kind == "generator" else 1.5, MAT["void_vein" if kind == "generator" else "void_crystal"], n=6)
    return {"hull": (m, (0, 0, 0))}, {}


def boulder(seed, flat=0.6):
    rnd = random.Random(seed)
    m = Mesh()
    n = 7
    ring = []
    for i in range(n):
        a = 2 * math.pi * i / n
        r = rnd.uniform(0.8, 1.2)
        ring.append((math.cos(a) * r, rnd.uniform(0.15, 0.35), math.sin(a) * r))
    top = (rnd.uniform(-0.2, 0.2), flat + rnd.uniform(0.0, 0.3), rnd.uniform(-0.2, 0.2))
    low = [(p[0] * 0.85, 0.0, p[2] * 0.85) for p in ring]
    for i in range(n):
        j = (i + 1) % n
        m.face([ring[i], top, ring[j]][::-1], MAT["rock"] if i % 3 else MAT["rock_dark"])
        m.face([low[i], ring[i], ring[j], low[j]][::-1], MAT["rock_dark"])
    if rnd.random() < 0.5:
        m.box((0.3, 0.45, 0.2), (0.15, 0.04, 0.3), MAT["moss"])
    return {"hull": (m, (0, 0, 0))}, {}


def dead_tree(seed):
    rnd = random.Random(seed)
    m = Mesh()
    h = rnd.uniform(2.2, 3.0)
    lean = (rnd.uniform(-0.3, 0.3), rnd.uniform(-0.3, 0.3))
    top = (lean[0], h, lean[1])
    m.strut((0, 0, 0), top, 0.12, MAT["bark"])
    for k in range(rnd.randint(3, 5)):
        t = rnd.uniform(0.4, 0.9)
        a = rnd.uniform(0, 2 * math.pi)
        start = (lean[0] * t, h * t, lean[1] * t)
        ln = rnd.uniform(0.5, 1.0) * (1.1 - t)
        end = (start[0] + math.cos(a) * ln, start[1] + ln * 0.7, start[2] + math.sin(a) * ln)
        m.strut(start, end, 0.05, MAT["charred"] if k % 2 else MAT["bark"])
    m.cyl((0, 0.05, 0), 0.25, 0.1, MAT["charred"], n=6, r2=0.12)
    return {"hull": (m, (0, 0, 0))}, {}


def crystal_growth(seed):
    rnd = random.Random(seed)
    m = Mesh()
    m.cyl((0, 0.05, 0), 0.7, 0.1, MAT["void_shell"], n=7, r2=0.5, capmat=MAT["void_purple"])
    for k in range(rnd.randint(3, 6)):
        a = rnd.uniform(0, 2 * math.pi)
        r = rnd.uniform(0.0, 0.45)
        m.crystal((math.cos(a) * r, 0.05, math.sin(a) * r), rnd.uniform(0.08, 0.18), rnd.uniform(0.5, 1.4),
                  MAT["void_crystal"] if k % 2 == 0 else MAT["void_vein"], tilt=(math.cos(a) * 0.3, math.sin(a) * 0.3), base=MAT["void_purple"])
    return {"hull": (m, (0, 0, 0))}, {}


def wreck(seed):
    rnd = random.Random(seed)
    m = Mesh()
    m.box((0, 0.3, 0), (1.3, 0.5, 2.0), MAT["charred"], top=(0.85, 0.8), shift=(0.1, 0.1), sides={"left": MAT["rust"]})
    m.box((0.85, 0.2, 0.1), (0.4, 0.4, 2.2), MAT["rust"], top=(1, 0.9))
    m.box((-0.2, 0.62, 0.3), (0.7, 0.25, 0.8), MAT["rust"], top=(0.8, 0.8))
    m.strut((-0.2, 0.7, 0.0), (-0.6, 0.45, -1.3), 0.06, MAT["charred"])
    for k in range(3):
        m.box((rnd.uniform(-1.4, 1.4), 0.06, rnd.uniform(-1.4, 1.4)), (rnd.uniform(0.2, 0.4), 0.1, rnd.uniform(0.2, 0.4)), MAT["rust"])
    m.cyl((0.1, 0.62, -0.5), 0.08, 0.06, MAT["ember"], n=5)
    return {"hull": (m, (0, 0, 0))}, {}


def fir(seed):
    rnd = random.Random(seed)
    m = Mesh()
    h = rnd.uniform(3.2, 4.4)
    m.cyl((0, 0.3, 0), 0.1, 0.6, MAT["bark"], n=6)
    tiers = rnd.randint(4, 5)
    for k in range(tiers):
        t = k / tiers
        y = 0.5 + t * (h - 0.9)
        r = (1.0 - t) * 1.05 + 0.18
        th = (h - 0.5) / tiers * 1.5
        m.cyl((0, y + th / 2, 0), r, th, MAT["fir"] if k % 2 else MAT["fir_dark"], n=7, r2=0.05, cap=False)
        m.cyl((0, y + th * 0.45, 0), r * 0.62, th * 0.32, MAT["snow"], n=7, r2=r * 0.18)
    return {"hull": (m, (0, 0, 0))}, {}


def birch(seed):
    rnd = random.Random(seed)
    m = Mesh()
    h = rnd.uniform(3.0, 4.2)
    lean = (rnd.uniform(-0.25, 0.25), rnd.uniform(-0.25, 0.25))
    m.strut((0, 0, 0), (lean[0], h, lean[1]), 0.09, MAT["birch"])
    for k in range(4):
        y = 0.4 + k * 0.6
        m.box((lean[0] * y / h, y, lean[1] * y / h), (0.2, 0.05, 0.2), MAT["charred"])
    for k in range(rnd.randint(5, 7)):
        t = rnd.uniform(0.45, 0.95)
        a = rnd.uniform(0, 2 * math.pi)
        start = (lean[0] * t, h * t, lean[1] * t)
        ln = rnd.uniform(0.5, 1.0) * (1.2 - t)
        end = (start[0] + math.cos(a) * ln, start[1] + ln * 0.9, start[2] + math.sin(a) * ln)
        m.strut(start, end, 0.035, MAT["bark"])
    return {"hull": (m, (0, 0, 0))}, {}


def old_car(seed):
    rnd = random.Random(seed)
    m = Mesh()
    paint = MAT["paint_old"] if seed % 2 else MAT["rust"]
    m.box((0, 0.42, 0), (1.0, 0.42, 2.3), paint, top=(1.0, 0.96))
    m.box((0, 0.8, 0.12), (0.92, 0.36, 1.1), paint, top=(0.86, 0.8), sides={"front": MAT["glass"], "back": MAT["glass"]})
    m.box((0, 1.0, 0.12), (0.8, 0.06, 0.9), MAT["snow"])
    m.box((0, 0.65, -0.82), (0.95, 0.05, 0.6), MAT["snow"])
    m.box((0, 0.65, 0.95), (0.9, 0.05, 0.36), MAT["snow"])
    for x in (-0.48, 0.48):
        for z in (-0.72, 0.72):
            m.cyl((x, 0.2, z), 0.2, 0.14, MAT["rubber"], n=8, axis="x")
    m.box((0, 0.36, -1.17), (0.9, 0.12, 0.05), MAT["steel"])
    if rnd.random() < 0.5:
        m.box((0.3, 0.06, 0.9), (0.6, 0.12, 0.8), MAT["snow"])
    return {"hull": (m, (0, 0, 0))}, {}


def power_pylon():
    m = Mesh()
    h = 7.0
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.strut((sx * 0.9, 0, sz * 0.9), (sx * 0.25, h, sz * 0.25), 0.07, MAT["steel"])
    for k in range(5):
        y = 0.8 + k * 1.3
        w = 0.9 - (0.65 * y / h)
        for a, b in (((-w, y, -w), (w, y + 1.3, -w)), ((w, y, w), (-w, y + 1.3, w)), ((-w, y, -w), (-w, y + 1.3, w)), ((w, y, w), (w, y + 1.3, -w))):
            m.strut(a, b, 0.035, MAT["graphite"])
    for y, w in ((h - 1.4, 2.4), (h - 0.2, 1.6)):
        m.box((0, y, 0), (w * 2, 0.14, 0.18), MAT["graphite"])
        for x in (-w, w):
            m.cyl((x, y - 0.3, 0), 0.06, 0.5, MAT["glass"], n=5)
    m.box((0, h + 0.05, 0), (0.4, 0.1, 0.4), MAT["snow"])
    return {"hull": (m, (0, 0, 0))}, {"wire0": ((-2.4, h - 1.75, 0), None), "wire1": ((2.4, h - 1.75, 0), None), "wire2": ((-1.6, h - 0.55, 0), None), "wire3": ((1.6, h - 0.55, 0), None)}


def street_lamp(lit):
    m = Mesh()
    m.cyl((0, 1.6, 0), 0.06, 3.2, MAT["graphite"], n=6)
    m.strut((0, 3.1, 0), (0.7, 3.3, 0), 0.05, MAT["graphite"])
    m.box((0.8, 3.25, 0), (0.36, 0.12, 0.2), MAT["graphite_dark"])
    m.box((0.8, 3.17, 0), (0.26, 0.04, 0.14), MAT["lamp"] if lit else MAT["glass"])
    m.box((0.8, 3.33, 0), (0.34, 0.04, 0.2), MAT["snow"])
    return {"hull": (m, (0, 0, 0))}, {}


def extractor():
    m = Mesh()
    m.cyl((0, 0.2, 0), 1.3, 0.4, MAT["concrete_dark"], n=8, capmat=MAT["concrete"])
    for i in range(4):
        a = math.pi / 4 + i * math.pi / 2
        m.strut((math.cos(a) * 1.0, 0.4, math.sin(a) * 1.0), (0.0, 2.6, 0.0), 0.07, MAT["hazard"] if i % 2 else MAT["graphite"])
    m.cyl((0, 1.1, 0), 0.35, 1.4, MAT["energy"], n=8)
    m.box((0, 2.65, 0), (0.6, 0.2, 0.6), MAT["graphite_dark"])
    m.cyl((0, 2.85, 0), 0.1, 0.2, MAT["warn"], n=6)
    m.box((0.95, 0.55, 0), (0.4, 0.3, 0.7), MAT["team"])
    return {"hull": (m, (0, 0, 0))}, {}


def module_gun(kind):
    tur, bar = Mesh(), Mesh()
    tur.box((0, 0.08, 0.05), (0.36, 0.16, 0.36), MAT["graphite_dark"], sides={"left": MAT["team"], "right": MAT["team"]})
    if kind == "mg":
        muzzle = cannon(bar, r=0.03, length=0.5, y=0.14, twin=0.05, brake=False)
    elif kind == "flak":
        muzzle = cannon(bar, r=0.045, length=0.55, y=0.16, twin=0.08, brake=True)
    elif kind == "aa":
        muzzle = rocket_pod(bar, rows=2, cols=2, y=0.2, size=0.08, length=0.45)
    else:
        bar.cyl((0.0, 0.18, -0.25), 0.13, 0.1, MAT["energy"], n=8, axis="z", r2=0.05)
        bar.box((0.0, 0.18, -0.05), (0.08, 0.08, 0.35), MAT["steel"])
        muzzle = (0.0, 0.18, -0.35)
    return {"turret": (tur, (0, 0, 0)), "barrel": (bar, (0, 0, 0))}, {"muzzle": (muzzle, "barrel")}


MODELS = {
    "module_mg": lambda: module_gun("mg"),
    "module_flak": lambda: module_gun("flak"),
    "module_aa": lambda: module_gun("aa"),
    "module_emp": lambda: module_gun("emp"),
    "colony_extractor": extractor,
    "prop_boulder1": lambda: boulder(1),
    "prop_boulder2": lambda: boulder(2, 0.4),
    "prop_boulder3": lambda: boulder(3, 0.9),
    "prop_tree1": lambda: dead_tree(1),
    "prop_tree2": lambda: dead_tree(2),
    "prop_tree3": lambda: dead_tree(3),
    "prop_crystal1": lambda: crystal_growth(1),
    "prop_crystal2": lambda: crystal_growth(2),
    "prop_wreck": lambda: wreck(1),
    "prop_fir1": lambda: fir(1),
    "prop_fir2": lambda: fir(2),
    "prop_fir3": lambda: fir(3),
    "prop_birch1": lambda: birch(1),
    "prop_birch2": lambda: birch(2),
    "prop_car1": lambda: old_car(1),
    "prop_car2": lambda: old_car(2),
    "prop_pylon": power_pylon,
    "prop_lamp": lambda: street_lamp(True),
    "prop_lamp_off": lambda: street_lamp(False),
    "colony_tank": lambda: tank("base"),
    "colony_tank_assault": lambda: tank("assault"),
    "colony_tank_siege": lambda: tank("siege"),
    "colony_tank_rocket": lambda: tank("rocket"),
    "colony_tank_artillery": lambda: tank("artillery"),
    "colony_tank_missile": lambda: tank("missile"),
    "colony_tank_radar": lambda: tank("radar"),
    "colony_tank_sensor": lambda: tank("sensor"),
    "colony_tank_command": lambda: tank("command"),
    "colony_tank_warlord": lambda: tank("warlord"),
    "colony_tank_repair": lambda: tank("repair"),
    "colony_tank_mechanic": lambda: tank("mechanic"),
    "colony_scout": lambda: scout("base"),
    "colony_scout_raider": lambda: scout("raider"),
    "colony_scout_hunter": lambda: scout("hunter"),
    "colony_scout_emp": lambda: scout("emp"),
    "colony_scout_jammer": lambda: scout("jammer"),
    "colony_engineer": engineer,
    "colony_gunship": lambda: gunship("base"),
    "colony_gunship_rocket": lambda: gunship("rocket"),
    "colony_gunship_interceptor": lambda: gunship("interceptor"),
    "colony_transport": air_transport,
    "colony_core": core,
    "colony_mg_tower": mg_tower,
    "colony_bunker": lambda: bunker(False),
    "colony_heavy_bunker": lambda: bunker(True),
    "colony_wall": wall,
    "colony_factory": factory,
    "colony_generator": generator,
    "colony_lab": lab,
    "void_crawler": lambda: void_walker(0.8, 2, 4, 1),
    "void_stalker": lambda: void_walker(0.75, 3, 4, 2),
    "void_brute": lambda: void_walker(1.1, 5, 6, 3),
    "void_warden": lambda: void_walker(1.4, 7, 6, 4),
    "void_titan": lambda: void_walker(1.8, 9, 8, 5),
    "void_spire": lambda: void_spire(2.6, 3),
    "void_bastion": lambda: void_spire(3.4, 4),
    "void_mortar": lambda: void_spire(2.0, 5),
    "void_citadel": lambda: void_spire(4.0, 6),
    "void_portal": portal,
    "void_pylon": pylon,
    "void_factory": lambda: void_hive("factory"),
    "void_generator": lambda: void_hive("generator"),
    "void_lab": lambda: void_hive("lab"),
}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/models"
    os.makedirs(out, exist_ok=True)
    color, mask = palette()
    with open(os.path.join(out, "palette.png"), "wb") as f:
        f.write(color)
    with open(os.path.join(out, "palette_mask.png"), "wb") as f:
        f.write(mask)
    for name, build in MODELS.items():
        parts, points = build()
        glb(os.path.join(out, name + ".glb"), parts, points, [("color", "palette.png"), ("mask", "palette_mask.png")])
    print(len(MODELS), "models written to", out)


if __name__ == "__main__":
    main()
