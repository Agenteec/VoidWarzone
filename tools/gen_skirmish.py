"""Skirmish map "Lead Reservoir": eight player islands in a Soviet reservoir, late autumn with the first snow.

python tools/gen_skirmish.py data/maps
Eight home islands on a ring, eight islets between them with guarded resource points, a central island with a
ruined settlement and the richest points; dams (open causeways with roads) link everything. Water cells are
CellKind 4 (blocked for ground units, rendered as a lowered lake bed + water surface by the client).
"""
import json
import math
import os
import random
import sys

import numpy as np

W = H = 240
C = (120.0, 120.0)
WATER, OPEN, ROCK = 4, 0, 1
rnd = random.Random(1987)


def ring(k, n, radius, phase=0.0):
    a = 2 * math.pi * k / n + phase
    return (C[0] + math.cos(a) * radius, C[1] + math.sin(a) * radius), a


def blob(grid, cx, cy, r, value, wobble=0.18, seed=0):
    rr = random.Random(seed)
    harm = [(rr.uniform(0, 6.28), rr.uniform(0.03, wobble)) for _ in range(5)]
    yy, xx = np.mgrid[0:H, 0:W]
    ang = np.arctan2(yy + 0.5 - cy, xx + 0.5 - cx)
    rad = r * (1.0 + sum(a * np.sin((i + 2) * ang + p) for i, (p, a) in enumerate(harm)))
    grid[np.hypot(xx + 0.5 - cx, yy + 0.5 - cy) < rad] = value


def stroke(grid, a, b, half, value):
    yy, xx = np.mgrid[0:H, 0:W]
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((xx + 0.5 - ax) * dx + (yy + 0.5 - ay) * dy) / (dx * dx + dy * dy), 0, 1)
    d = np.hypot(xx + 0.5 - (ax + t * dx), yy + 0.5 - (ay + t * dy))
    grid[d < half] = value


def rect(cx, cy, w, h, angle):
    c, s = math.cos(angle), math.sin(angle)
    pts = []
    for x, y in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        pts.append([round(cx + x * c - y * s, 2), round(cy + x * s + y * c, 2)])
    return pts


def circle(cx, cy, r, n=10):
    return [[round(cx + math.cos(2 * math.pi * k / n) * r * rnd.uniform(0.8, 1.1), 2), round(cy + math.sin(2 * math.pi * k / n) * r * rnd.uniform(0.8, 1.1), 2)]
            for k in range(n)]


def rle(grid):
    flat = grid.flatten()
    out, i = [], 0
    while i < len(flat):
        j = i
        while j < len(flat) and flat[j] == flat[i]:
            j += 1
        out.append(f"{int(flat[i])}:{j - i}")
        i = j
    return ",".join(out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/maps"
    grid = np.full((H, W), WATER, dtype=np.int32)
    homes, islets = [], []
    for k in range(8):
        (x, y), a = ring(k, 8, 86)
        homes.append((x, y, a))
        blob(grid, x, y, 21, OPEN, wobble=0.07, seed=10 + k)
        (ix, iy), ia = ring(k, 8, 62, math.pi / 8)
        islets.append((ix, iy, ia))
        blob(grid, ix, iy, 8.5, OPEN, wobble=0.12, seed=40 + k)
    blob(grid, C[0], C[1], 31, OPEN, wobble=0.08, seed=99)

    dams = []
    for k in range(8):
        hx, hy, _ = homes[k]
        for j in (k, (k - 1) % 8):
            ix, iy, _ = islets[j]
            dams.append(((hx, hy), (ix, iy)))
        ix, iy, _ = islets[k]
        dams.append(((ix, iy), C))
    for a, b in dams:
        stroke(grid, a, b, 1.8, OPEN)

    buildings, areas, paths, props, units, points = [], [], [], [], [], []
    occupied = np.zeros((H, W), dtype=bool)

    def cells_of(poly, pad=1.0):
        cx = sum(p[0] for p in poly) / len(poly)
        cy = sum(p[1] for p in poly) / len(poly)
        grown = [[cx + (p[0] - cx) * (1 + pad / 3), cy + (p[1] - cy) * (1 + pad / 3)] for p in poly]
        xs = [p[0] for p in grown]
        ys = [p[1] for p in grown]
        out = []
        for y in range(int(min(ys)), int(max(ys)) + 1):
            for x in range(int(min(xs)), int(max(xs)) + 1):
                px, py, inside = x + 0.5, y + 0.5, False
                for i in range(len(grown)):
                    (ax, ay), (bx, by) = grown[i], grown[i - 1]
                    if (ay > py) != (by > py) and px < (bx - ax) * (py - ay) / (by - ay) + ax:
                        inside = not inside
                if inside:
                    out.append((x, y))
        return out

    def free(poly, pad=1.0):
        cells = cells_of(poly, pad)
        return bool(cells) and all(0 < x < W - 1 and 0 < y < H - 1 and grid[y, x] == OPEN and not occupied[y, x] for x, y in cells)

    def claim(poly, pad=1.0):
        for x, y in cells_of(poly, pad):
            occupied[y, x] = True

    def building(cx, cy, w, h, angle, **kw):
        poly = rect(cx, cy, w, h, angle)
        if not free(poly):
            return False
        claim(poly)
        b = {"poly": poly}
        b.update(kw)
        buildings.append(b)
        return True

    # keep starts, dams and points clear
    starts = []
    for k, (hx, hy, a) in enumerate(homes):
        sx, sy = hx - math.cos(a) * 4, hy - math.sin(a) * 4
        starts.append([round(sx, 1), round(sy, 1)])
        yy, xx = np.mgrid[0:H, 0:W]
        occupied[np.hypot(xx - sx, yy - sy) < 10] = True
    for a, b in dams:
        tmp = np.zeros((H, W), dtype=np.int32)
        stroke(tmp, a, b, 3.0, 1)
        occupied |= tmp.astype(bool)

    for k, (hx, hy, a) in enumerate(homes):
        out_x, out_y = math.cos(a), math.sin(a)
        side_x, side_y = -out_y, out_x
        for i, off in enumerate((-9, 0, 9)):
            cx = hx + out_x * 11 + side_x * off
            cy = hy + out_y * 11 + side_y * off
            kind = "panel" if i != 1 else ("garage" if k % 2 else "house")
            if kind == "panel":
                building(cx, cy, 8, 3, a + math.pi / 2, levels=5 if (k + i) % 3 else 9, kind="panel", ruin=rnd.random() < 0.25)
            elif kind == "garage":
                building(cx, cy, 8, 2.2, a + math.pi / 2, levels=1, kind="garage")
            else:
                building(cx, cy, 3, 3, a, levels=2, kind="house")
        for off in (-15, 15):
            fx, fy = hx + out_x * 5 + side_x * off, hy + out_y * 5 + side_y * off
            poly = circle(fx, fy, 3.2)
            if free(poly, 0):
                claim(poly, 0)
                areas.append({"kind": "forest", "poly": poly})
        rx, ry = hx + side_x * 11 - out_x * 3, hy + side_y * 11 - out_y * 3
        points.append({"name": f"H{k + 1}", "pos": [round(rx, 1), round(ry, 1)], "radius": 3.0, "income": 2})
        props.append({"model": "prop_water_tower" if k % 2 else "prop_substation", "pos": [round(hx + out_x * 17 - side_x * 14, 1), round(hy + out_y * 17 - side_y * 14, 1)],
                      "yaw": round(math.degrees(a), 1), "radius": 1.2})

    for k, (ix, iy, a) in enumerate(islets):
        points.append({"name": "ABCDEFGH"[k], "pos": [round(ix, 1), round(iy, 1)], "radius": 3.5, "income": 3})
        units.append({"unit": "raider_bunker", "team": 1, "pos": [round(ix + 3.5, 1), round(iy + 2.5, 1)]})
        units.append({"unit": "raider_technical" if k % 2 else "raider_gatling", "team": 1, "pos": [round(ix - 2.5, 1), round(iy - 3, 1)]})

    for i in range(8):
        a = 2 * math.pi * i / 8
        for r in (20, 17, 23, 14):
            cx, cy = C[0] + math.cos(a) * r, C[1] + math.sin(a) * r
            if i == 2:
                ok = building(cx, cy, 9, 6, a + math.pi / 2, levels=2, kind="industrial")
            else:
                ok = building(cx, cy, 7, 3, a + math.pi / 2, levels=9 if i % 2 else 5, kind="panel", ruin=i % 3 != 0)
            if ok:
                break
    props.append({"model": "prop_cell_tower", "pos": [C[0] + 21, C[1] - 12], "yaw": 0, "radius": 1.0})
    props.append({"model": "prop_gas_station", "pos": [C[0] - 20, C[1] - 6], "yaw": 90, "radius": 2.0})
    for i, (dx, dy) in enumerate(((8, 0), (-8, 0))):
        points.append({"name": "Center" + str(i + 1), "pos": [C[0] + dx, C[1] + dy], "radius": 4.0, "income": 5})
    units += [{"unit": "raider_camp", "team": 1, "pos": [C[0], C[1] - 4]},
              {"unit": "raider_bunker", "team": 1, "pos": [C[0] + 5, C[1] + 5]},
              {"unit": "raider_bunker", "team": 1, "pos": [C[0] - 5, C[1] + 5]},
              {"unit": "raider_rocket", "team": 1, "pos": [C[0], C[1] + 2]},
              {"unit": "raider_mortar", "team": 1, "pos": [C[0] + 3, C[1] - 7]}]

    for a, b in dams:
        paths.append({"kind": "road", "width": 1.6, "points": [[round(a[0], 1), round(a[1], 1)], [round(b[0], 1), round(b[1], 1)]]})
    ring_pts = [[round(C[0] + math.cos(2 * math.pi * k / 16) * 10, 1), round(C[1] + math.sin(2 * math.pi * k / 16) * 10, 1)] for k in range(17)]
    paths.append({"kind": "road", "width": 1.4, "points": ring_pts})
    for k, (hx, hy, a) in enumerate(homes):
        paths.append({"kind": "power", "width": 0.5, "points": [[round(hx + math.cos(a) * 15, 1), round(hy + math.sin(a) * 15, 1)],
                                                              [round(hx - math.sin(a) * 15, 1), round(hy + math.cos(a) * 15, 1)]]})

    for k in range(20):
        (x, y), _ = ring(k, 20, rnd.uniform(70, 100), rnd.uniform(0, 0.2))
        if grid[int(y), int(x)] == OPEN and not occupied[int(y), int(x)]:
            blob(grid, x, y, rnd.uniform(1.2, 2.2), ROCK, seed=200 + k)
            occupied[max(0, int(y) - 3):int(y) + 4, max(0, int(x) - 3):int(x) + 4] = True

    doc = {
        "name": "Lead Reservoir",
        "width": W,
        "height": H,
        "theme": "autumn",
        "cells": rle(grid),
        "playerStart": {"pos": starts[0], "units": ["kshm", "builder_truck", "builder_truck", "uaz", "uaz", "mtlb"], "slots": starts},
        "defense": {"startEnergy": 600, "baseIncome": 2.0, "waves": []},
        "resourcePoints": points,
        "units": units,
        "buildings": buildings,
        "areas": areas,
        "paths": paths,
        "props": props,
        "lanes": [],
    }
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "lead_reservoir.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
    land = int((grid != WATER).sum())
    print(f"lead_reservoir: {len(buildings)} buildings, {len(points)} points, land {land}/{W * H}")


if __name__ == "__main__":
    main()
