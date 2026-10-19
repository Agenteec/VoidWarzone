"""Skirmish map "Frozen Oblast": 16 bases around a ruined regional centre, two frozen rivers cross the map.

python tools/gen_oblast.py data/maps
448x448 cells. Sixteen bases on a ring (four per quarter), the rivers run corner to corner and meet in a lake
around the island city in the middle (four dams, the richest points, a raider stronghold). Every quarter has
villages, a collective farm and a guarded depot between its bases; bridges over the rivers are chokepoints.
"""
import json
import math
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_skirmish import OPEN, ROCK, WATER, blob, rect, rle  # noqa: E402

W = H = 448
C = (224.0, 224.0)
rnd = random.Random(1991)
YY, XX = np.mgrid[0:H, 0:W]


def polar(radius, angle):
    return (C[0] + math.cos(angle) * radius, C[1] + math.sin(angle) * radius)


def stroke(grid, a, b, half, value):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((XX + 0.5 - ax) * dx + (YY + 0.5 - ay) * dy) / max(dx * dx + dy * dy, 1e-6), 0, 1)
    grid[np.hypot(XX + 0.5 - (ax + t * dx), YY + 0.5 - (ay + t * dy)) < half] = value


def circle(cx, cy, r, n=10):
    return [[round(cx + math.cos(2 * math.pi * k / n) * r * rnd.uniform(0.8, 1.1), 2),
             round(cy + math.sin(2 * math.pi * k / n) * r * rnd.uniform(0.8, 1.1), 2)] for k in range(n)]


def river_point(k, t):
    # arm k (0..3) leaves the central lake along a diagonal and wiggles towards a corner
    a = math.pi / 4 + k * math.pi / 2
    r = 44 + t * 290
    side = 9 * math.sin(t * 9.0 + k) + 4 * math.sin(t * 23.0 + 2 * k)
    x, y = polar(r, a)
    return x - math.sin(a) * side, y + math.cos(a) * side


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/maps"
    grid = np.full((H, W), OPEN, dtype=np.int32)
    occupied = np.zeros((H, W), dtype=bool)
    buildings, areas, paths, props, units, points = [], [], [], [], [], []

    # rivers and the central lake ring with the island city
    for k in range(4):
        pts = [river_point(k, i / 60) for i in range(61)]
        for a, b in zip(pts, pts[1:]):
            stroke(grid, a, b, 5.0, WATER)
    d = np.hypot(XX + 0.5 - C[0], YY + 0.5 - C[1])
    grid[(d > 36) & (d < 48)] = WATER
    bridges = []
    for k in range(4):
        a = k * math.pi / 2
        stroke(grid, polar(32, a), polar(52, a), 3.5, OPEN)
        paths.append({"kind": "road", "width": 3.0, "points": [list(map(lambda v: round(v, 1), polar(26, a))), list(map(lambda v: round(v, 1), polar(58, a)))]})
        occupied[np.hypot(XX - polar(42, a)[0], YY - polar(42, a)[1]) < 9] = True
    for k in range(4):
        for t in (0.22, 0.48, 0.74):
            x, y = river_point(k, t)
            x2, y2 = river_point(k, t + 0.01)
            nx, ny = -(y2 - y), x2 - x
            n = math.hypot(nx, ny)
            nx, ny = nx / n, ny / n
            stroke(grid, (x - nx * 12, y - ny * 12), (x + nx * 12, y + ny * 12), 3.2, OPEN)
            bridges.append((x, y, nx, ny))
            paths.append({"kind": "road", "width": 2.6, "points": [[round(x - nx * 20, 1), round(y - ny * 20, 1)], [round(x + nx * 20, 1), round(y + ny * 20, 1)]]})
            occupied[np.hypot(XX - x, YY - y) < 12] = True

    # lakes, rock ridges and the map edge
    for k in range(26):
        a = rnd.uniform(0, 2 * math.pi)
        x, y = polar(rnd.uniform(70, 215), a)
        blob(grid, x, y, rnd.uniform(3, 7), ROCK, seed=200 + k)
    edge = (XX < 3) | (YY < 3) | (XX >= W - 3) | (YY >= H - 3)
    grid[edge] = ROCK
    for k in range(4):
        corner = [(10, 10), (W - 10, 10), (W - 10, H - 10), (10, H - 10)][k]
        blob(grid, corner[0], corner[1], 20, ROCK, seed=300 + k)

    # 16 bases on a ring between the river arms (four per quarter)
    starts = []
    for k in range(16):
        a = math.pi / 16 + k * math.pi / 8
        x, y = polar(178, a)
        starts.append([round(x, 1), round(y, 1)])
        clear = np.hypot(XX + 0.5 - x, YY + 0.5 - y) < 17
        grid[clear & (grid != WATER)] = OPEN
        grid[clear & (grid == WATER) & (np.hypot(XX + 0.5 - x, YY + 0.5 - y) < 13)] = OPEN
        occupied[np.hypot(XX - x, YY - y) < 13] = True

    def free(poly):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        x0, x1, y0, y1 = int(min(xs)) - 1, int(max(xs)) + 1, int(min(ys)) - 1, int(max(ys)) + 1
        if x0 < 4 or y0 < 4 or x1 >= W - 4 or y1 >= H - 4:
            return False
        return bool((grid[y0:y1 + 1, x0:x1 + 1] == OPEN).all() and not occupied[y0:y1 + 1, x0:x1 + 1].any())

    def building(cx, cy, w, h, angle=0.0, **kw):
        poly = rect(cx, cy, w, h, angle)
        if not free(poly):
            return False
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        occupied[int(min(ys)) - 1:int(max(ys)) + 2, int(min(xs)) - 1:int(max(xs)) + 2] = True
        b = {"poly": poly}
        b.update(kw)
        buildings.append(b)
        return True

    def guard(x, y, kinds):
        for i, u in enumerate(kinds):
            a = 2 * math.pi * i / max(1, len(kinds))
            units.append({"unit": u, "team": 1, "pos": [round(x + math.cos(a) * 3, 1), round(y + math.sin(a) * 3, 1)]})

    # island city: panel blocks around a square, the richest points and a stronghold
    for i in range(14):
        a = 2 * math.pi * i / 14
        r = 22 if i % 2 else 15
        x, y = polar(r, a + 0.2)
        building(x, y, 9 if i % 2 else 6, 3, a + math.pi / 2, levels=9 if i % 3 else 12, kind="panel", ruin=rnd.random() < 0.6)
    for i, a in enumerate((0.0, 2.1, 4.2)):
        x, y = polar(9, a)
        points.append({"name": "City" + str(i + 1), "pos": [round(x, 1), round(y, 1)], "radius": 3.5, "income": 6})
    units += [{"unit": "raider_camp", "team": 1, "pos": [C[0], C[1]]}]
    guard(C[0], C[1], ["raider_bunker", "raider_gatling", "raider_rocket", "raider_bunker", "raider_mortar"])
    areas.append({"kind": "industrial", "poly": [list(polar(30, a * math.pi / 4)) for a in range(8)]})

    # bridges carry small guarded points
    for i, (x, y, nx, ny) in enumerate(bridges):
        if i % 3 == 1 and grid[int(y), int(x)] == OPEN:
            points.append({"name": "Bridge" + str(i + 1), "pos": [round(x, 1), round(y, 1)], "radius": 3.0, "income": 3})
            units.append({"unit": "raider_technical", "team": 1, "pos": [round(x + nx * 3, 1), round(y + ny * 3, 1)]})

    # middle ring: depots and collective farms between neighbouring bases, villages towards the centre
    for k in range(16):
        a = k * math.pi / 8 + math.pi / 8
        x, y = polar(150, a)
        on_river = k % 4 == 1
        if not on_river:
            grid[(np.hypot(XX + 0.5 - x, YY + 0.5 - y) < 9) & (grid == ROCK)] = OPEN
            points.append({"name": "Depot" + str(k + 1), "pos": [round(x, 1), round(y, 1)], "radius": 3.0, "income": 3})
            if k % 2 == 0:
                guard(x, y, ["raider_bunker", "raider_buggy"])
            for j in range(3):
                bx, by = x + math.cos(a + 1.2 + j * 2.1) * 8, y + math.sin(a + 1.2 + j * 2.1) * 8
                building(bx, by, 7, 4, a, levels=1 if j else 2, kind="industrial", ruin=j == 2)
        vx, vy = polar(105, a - math.pi / 16)
        points.append({"name": "Village" + str(k + 1), "pos": [round(vx, 1), round(vy, 1)], "radius": 3.0, "income": 2})
        for j in range(6):
            hx, hy = vx + math.cos(j * 1.05) * 9, vy + math.sin(j * 1.05) * 9
            building(hx, hy, 3, 3, j * 0.4, levels=1 if j % 2 else 2, kind="house", ruin=rnd.random() < 0.3)
        if k % 4 == 1:
            guard(vx, vy, ["raider_camp", "raider_mortar"])

    # lakes away from points, bases and buildings
    placed = 0
    for k in range(60):
        if placed >= 12:
            break
        a = rnd.uniform(0, 2 * math.pi)
        x, y = polar(rnd.uniform(70, 200), a)
        m = (a - math.pi / 4) % (math.pi / 2)
        r = rnd.uniform(6, 11)
        if m < 0.2 or m > math.pi / 2 - 0.2 or occupied[np.hypot(XX - x, YY - y) < r * 1.3 + 2].any():
            continue
        if any(math.hypot(p["pos"][0] - x, p["pos"][1] - y) < r * 1.3 + 8 for p in points) or                 any(math.hypot(sx - x, sy - y) < r * 1.3 + 20 for sx, sy in starts):
            continue
        blob(grid, x, y, r, WATER, seed=100 + k)
        placed += 1

    # home bases
    for k, (sx, sy) in enumerate(starts):
        a = math.atan2(sy - C[1], sx - C[0])
        ox, oy = math.cos(a), math.sin(a)
        tx, ty = -oy, ox
        points.append({"name": "H" + str(k + 1), "pos": [round(sx - ox * 11 + tx * 6, 1), round(sy - oy * 11 + ty * 6, 1)], "radius": 3.0, "income": 2})
        occupied[np.hypot(XX - sx, YY - sy) < 13] = False
        occupied[np.hypot(XX - sx, YY - sy) < 10] = True
        building(sx + ox * 14 + tx * 8, sy + oy * 14 + ty * 8, 9, 3, a + math.pi / 2, kind="panel", levels=5, ruin=k % 4 == 2)
        building(sx + ox * 12 - tx * 10, sy + oy * 12 - ty * 10, 3, 8, a + math.pi / 2, kind="garage", levels=1)
        areas.append({"kind": "forest", "poly": circle(sx + ox * 22 - tx * 14, sy + oy * 22 - ty * 14, 6)})

    # roads: outer ring through the bases, middle ring through the depots, radials to the city
    ring = [[round(v, 1) for v in polar(178, k * math.pi / 32)] for k in range(65)]
    paths.append({"kind": "road", "width": 2.2, "points": ring})
    paths.append({"kind": "road", "width": 1.8, "points": [[round(v, 1) for v in polar(130, k * math.pi / 32)] for k in range(65)]})
    for k in range(4):
        a = k * math.pi / 2
        paths.append({"kind": "road", "width": 2.6, "points": [[round(v, 1) for v in polar(r, a)] for r in (56, 100, 150, 200)]})
    paths.append({"kind": "rail", "width": 1.0, "points": [[0, 120], [120, 96], [224, 70], [330, 96], [W, 120]]})
    paths.append({"kind": "power", "width": 0.5, "points": [[0, 330], [110, 350], [224, 372], [340, 352], [W, 330]]})

    # forests
    placed = 0
    for _ in range(400):
        if placed >= 70:
            break
        x, y = rnd.uniform(12, W - 12), rnd.uniform(12, H - 12)
        r = rnd.uniform(4, 9)
        poly = circle(x, y, r)
        if free(poly):
            areas.append({"kind": "forest", "poly": poly})
            placed += 1
    props += [{"model": "prop_water_tower", "pos": list(polar(28, 0.8)), "yaw": 0, "radius": 1.2},
              {"model": "prop_cell_tower", "pos": list(polar(28, 3.9)), "yaw": 0, "radius": 1.0}]
    for p in props:
        p["pos"] = [round(v, 1) for v in p["pos"]]

    doc = {
        "name": "Frozen Oblast",
        "width": W,
        "height": H,
        "theme": "winter",
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
    with open(os.path.join(out, "frozen_oblast.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(f"frozen_oblast: {len(buildings)} buildings, {len(points)} points, {len(units)} guards")


if __name__ == "__main__":
    main()
