"""Skirmish map "Dead Kombinat": a frozen river splits a dead industrial town, six bases, three bridges.

python tools/gen_kombinat.py data/maps
North bank: the plant (fenced workshops, water tower, chimney stacks) between three bases; south bank: a ruined
panel district between three more. Bridges are the chokepoints and carry guarded resource points.
"""
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_skirmish import OPEN, ROCK, WATER, blob, circle, rect, rle  # noqa: E402

W, H = 224, 176


def stroke(grid, a, b, half, value):
    yy, xx = np.mgrid[0:H, 0:W]
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = np.clip(((xx + 0.5 - ax) * dx + (yy + 0.5 - ay) * dy) / max(dx * dx + dy * dy, 1e-6), 0, 1)
    grid[np.hypot(xx + 0.5 - (ax + t * dx), yy + 0.5 - (ay + t * dy)) < half] = value


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/maps"
    grid = np.full((H, W), OPEN, dtype=np.int32)
    river = [(x, 88 + 14 * math.sin(x / 31.0) + 5 * math.sin(x / 11.0)) for x in range(-8, W + 9, 4)]
    for a, b in zip(river, river[1:]):
        stroke(grid, a, b, 5.5, WATER)
    blob(grid, 18, 158, 12, WATER, seed=3)
    for k, (x, y, r) in enumerate(((12, 12, 9), (212, 14, 8), (210, 164, 10), (70, 10, 5), (150, 168, 5))):
        blob(grid, x, y, r, ROCK, seed=60 + k)

    def river_y(x):
        return 88 + 14 * math.sin(x / 31.0) + 5 * math.sin(x / 11.0)

    bridges = [40, 112, 184]
    for bx in bridges:
        ry = river_y(bx)
        stroke(grid, (bx, ry - 11), (bx, ry + 11), 3.5, OPEN)

    starts = [[36, 28], [112, 22], [188, 28], [36, 150], [112, 156], [188, 150]]
    buildings, areas, paths, props, units, points = [], [], [], [], [], []
    occupied = np.zeros((H, W), dtype=bool)
    yy, xx = np.mgrid[0:H, 0:W]
    for sx, sy in starts:
        occupied[np.hypot(xx - sx, yy - sy) < 11] = True
    for bx in bridges:
        ry = river_y(bx)
        occupied[int(ry - 16):int(ry + 17), bx - 6:bx + 7] = True

    def free(poly):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        x0, x1, y0, y1 = int(min(xs)) - 1, int(max(xs)) + 1, int(min(ys)) - 1, int(max(ys)) + 1
        if x0 < 1 or y0 < 1 or x1 >= W - 1 or y1 >= H - 1:
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

    # the plant on the north bank
    plant = [(96, 52, 16, 9), (124, 50, 12, 9), (110, 66, 22, 7), (86, 68, 8, 8), (136, 66, 9, 7)]
    for i, (x, y, w, h) in enumerate(plant):
        building(x, y, w, h, levels=3 if i == 2 else 2, kind="industrial", ruin=i == 3)
    areas.append({"kind": "industrial", "poly": [[78, 42], [146, 42], [146, 74], [78, 74]]})
    props += [{"model": "prop_water_tower", "pos": [146, 46], "yaw": 0, "radius": 1.2},
              {"model": "prop_substation", "pos": [76, 54], "yaw": 90, "radius": 1.5},
              {"model": "prop_crane", "pos": [140, 76], "yaw": 30, "radius": 0.0}]
    points.append({"name": "Plant", "pos": [110, 58], "radius": 4.0, "income": 5})
    units += [{"unit": "raider_bunker", "team": 1, "pos": [104, 58]}, {"unit": "raider_bunker", "team": 1, "pos": [117, 59]},
              {"unit": "raider_gatling", "team": 1, "pos": [110, 54]}]

    # ruined district on the south bank
    for i, (x, y, w, h) in enumerate(((92, 118, 14, 3), (122, 116, 14, 3), (90, 132, 3, 12), (134, 130, 3, 12), (112, 136, 12, 3))):
        building(x, y, w, h, levels=9 if i % 2 else 5, kind="panel", ruin=i != 4)
    points.append({"name": "District", "pos": [112, 126], "radius": 4.0, "income": 5})
    units += [{"unit": "raider_camp", "team": 1, "pos": [112, 122]}, {"unit": "raider_rocket", "team": 1, "pos": [106, 128]},
              {"unit": "raider_bunker", "team": 1, "pos": [118, 130]}]

    # bridges: guarded points, roads across
    for i, bx in enumerate(bridges):
        ry = river_y(bx)
        points.append({"name": "Bridge" + "WCE"[i], "pos": [bx, round(ry, 1)], "radius": 3.0, "income": 3})
        units.append({"unit": "raider_technical", "team": 1, "pos": [bx + 1.5, round(ry - 2, 1)]})
        paths.append({"kind": "road", "width": 3.0, "points": [[bx, round(ry - 22, 1)], [bx, round(ry + 22, 1)]]})

    # home bases: garages, houses, a panel block each
    for k, (sx, sy) in enumerate(starts):
        north = sy < 88
        out_y = -1 if north else 1
        building(sx - 14, sy + out_y * 3, 3, 9, kind="garage", levels=1)
        building(sx + 14, sy + out_y * 2, 3, 3, kind="house", levels=2)
        building(sx + 4, sy + out_y * 12, 10, 3, kind="panel", levels=5, ruin=k % 3 == 1)
        points.append({"name": "H" + str(k + 1), "pos": [sx - 8, sy - out_y * 10], "radius": 3.0, "income": 2})
        poly = circle(sx + 16, sy - out_y * 12, 4)
        areas.append({"kind": "forest", "poly": poly})

    paths.append({"kind": "road", "width": 2.4, "points": [[0, 44], [60, 40], [112, 36], [170, 40], [W, 44]]})
    paths.append({"kind": "road", "width": 2.4, "points": [[0, 134], [60, 138], [112, 142], [170, 138], [W, 134]]})
    paths.append({"kind": "rail", "width": 1.0, "points": [[0, 74], [70, 76], [150, 78], [W, 74]]})
    paths.append({"kind": "power", "width": 0.5, "points": [[0, 104], [80, 110], [160, 104], [W, 108]]})
    for x, y, r in ((60, 112, 5), (166, 120, 6), (24, 96, 4), (200, 70, 5), (60, 62, 4)):
        poly = circle(x, y, r)
        if free(poly):
            areas.append({"kind": "forest", "poly": poly})

    doc = {
        "name": "Dead Kombinat",
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
    with open(os.path.join(out, "dead_kombinat.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(f"dead_kombinat: {len(buildings)} buildings, {len(points)} points")


if __name__ == "__main__":
    main()
