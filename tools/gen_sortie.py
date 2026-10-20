"""Chapter 1 sortie "Rail Depot": a marauder fuel depot at a frozen rail yard east of Voronezh.

python tools/gen_sortie.py data/maps
The player arrives from the west with the units picked for the sortie (or a small default column), clears the
patrols, destroys the depot and recovers the rocket documents from the signal box (an artifact: grants research).
"""
import json
import math
import os
import random
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_skirmish import OPEN, ROCK, WATER, blob, rect, rle  # noqa: E402

W, H = 200, 140
rnd = random.Random(1943)


def unit(u, pos, team=1, tag=None):
    d = {"unit": u, "team": team, "pos": [round(pos[0], 1), round(pos[1], 1)]}
    if tag:
        d["tag"] = tag
    return d


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/maps"
    grid = np.full((H, W), OPEN, dtype=np.int32)
    for k, (x, y, r) in enumerate(((20, 12, 10), (180, 128, 12), (60, 132, 8), (110, 8, 7))):
        blob(grid, x, y, r, ROCK, seed=10 + k)
    blob(grid, 70, 112, 9, WATER, seed=20)
    grid[:3, :] = ROCK
    grid[-3:, :] = ROCK
    grid[:, :3] = ROCK
    grid[:, -3:] = ROCK

    buildings, areas, paths, props, units = [], [], [], [], []
    occupied = np.zeros((H, W), dtype=bool)
    for x, y, r in ((18, 70, 14), (150, 70, 12), (120, 40, 7)):
        yy, xx = np.mgrid[0:H, 0:W]
        occupied[np.hypot(xx - x, yy - y) < r] = True

    def building(cx, cy, w, h, angle=0.0, **kw):
        poly = rect(cx, cy, w, h, angle)
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        x0, x1, y0, y1 = int(min(xs)) - 1, int(max(xs)) + 1, int(min(ys)) - 1, int(max(ys)) + 1
        if x0 < 4 or y0 < 4 or x1 >= W - 4 or y1 >= H - 4:
            return False
        if not (grid[y0:y1 + 1, x0:x1 + 1] == OPEN).all() or occupied[y0:y1 + 1, x0:x1 + 1].any():
            return False
        occupied[y0:y1 + 1, x0:x1 + 1] = True
        b = {"poly": poly}
        b.update(kw)
        buildings.append(b)
        return True

    # rail yard: three parallel tracks with wagons (wrecked cars as props) and warehouses along them
    for k, y in enumerate((60, 66, 80)):
        paths.append({"kind": "rail", "width": 1.0, "points": [[0, y], [W, y + (k - 1) * 2]]})
    paths.append({"kind": "road", "width": 2.6, "points": [[0, 72], [60, 74], [120, 92], [W, 96]]})
    paths.append({"kind": "road", "width": 2.0, "points": [[120, 92], [124, 50], [130, 20]]})
    paths.append({"kind": "power", "width": 0.5, "points": [[0, 30], [90, 34], [W, 28]]})
    for i in range(7):
        building(50 + i * 16, 52, 11, 5, kind="industrial", levels=2, ruin=i % 3 == 1)
    for i in range(5):
        building(60 + i * 20, 88 + (i % 2) * 2, 12, 6, kind="industrial", levels=1 + i % 2, ruin=i == 3)
    building(120, 40, 4, 4, kind="house", levels=2)
    for x, y, w, h, kind in ((30, 100, 3, 9, "garage"), (40, 104, 3, 9, "garage"), (90, 20, 10, 3, "panel"), (150, 116, 10, 3, "panel")):
        building(x, y, w, h, kind=kind, levels=5 if kind == "panel" else 1, ruin=True)
    for x, y in ((140, 60), (146, 58), (156, 62), (162, 64), (170, 61)):
        props.append({"model": "prop_water_tower", "pos": [x, y], "yaw": 0, "radius": 1.0})
    for k in range(10):
        x = 40 + k * 14 + rnd.uniform(-3, 3)
        props.append({"model": "prop_car_burnt1" if k % 2 else "prop_bus_burnt", "pos": [round(x, 1), 66 if k % 2 else 60], "yaw": 90, "radius": 0.0})
    placed = 0
    for _ in range(300):
        if placed >= 26:
            break
        x, y = rnd.uniform(8, W - 8), rnd.uniform(8, H - 8)
        if 50 < y < 100 or occupied[int(y), int(x)]:
            continue
        r = rnd.uniform(3, 7)
        poly = [[round(x + math.cos(2 * math.pi * k / 10) * r * rnd.uniform(0.8, 1.1), 2),
                 round(y + math.sin(2 * math.pi * k / 10) * r * rnd.uniform(0.8, 1.1), 2)] for k in range(10)]
        areas.append({"kind": "forest", "poly": poly})
        placed += 1
    areas.append({"kind": "industrial", "poly": [[44, 46], [196, 46], [196, 100], [44, 100]]})

    # marauders: patrols on the approach, the depot camp by the fuel tanks, the signal box guard
    units += [unit("raider_buggy", (62, 74)), unit("raider_buggy", (66, 76)), unit("raider_technical", (90, 82)),
              unit("raider_bunker", (100, 70)), unit("raider_gatling", (104, 74))]
    units += [unit("raider_camp", (152, 72), tag="depot"), unit("raider_bunker", (144, 76), tag="depot"), unit("raider_bunker", (160, 78), tag="depot"),
              unit("raider_rocket", (156, 68)), unit("raider_mortar", (162, 72)), unit("raider_mortar", (148, 68)),
              unit("raider_technical", (150, 82)), unit("raider_gatling", (166, 76))]
    units += [unit("raider_bunker", (124, 44)), unit("raider_technical", (116, 46)), unit("raider_buggy", (126, 36))]

    mission = {
        "title": "Rail Depot",
        "briefing": "The convoy drivers told us where the gangs get their fuel: the old rail depot east of the city. Take a strike group along "
                    "the frozen tracks - the base stays behind under its towers. Burn the depot, and search the signal box: the depot served the "
                    "artillery works, and their papers could teach our lab to build rockets. Whoever comes back returns to the base with all "
                    "their experience.",
        "objectives": [
            {"id": "depot", "text": "Destroy the marauder fuel depot", "type": "destroy", "tag": "depot"},
            {"id": "docs", "text": "Recover the documents from the signal box", "type": "reach", "pos": [120, 40], "radius": 5},
        ],
        "triggers": [
            {"when": "start", "do": [{"type": "message", "text": "Strike group is on the tracks. The depot is east, the signal box north of the yard."}]},
            {"when": "objective", "objective": "docs", "do": [
                {"type": "message", "text": "Documents recovered: Grad rocket artillery. The lab can build on this."},
                {"type": "research", "topic": "rocketry"}]},
            {"when": "objective", "objective": "depot", "do": [{"type": "message", "text": "The depot is burning. Their trucks will run dry by spring. The prisoners keep talking about a violet glow in the south."}]},
            {"when": "time", "time": 120, "repeat": 90, "times": 4, "do": [
                {"type": "message", "text": "Marauder trucks coming down the eastern road!"},
                {"type": "spawn", "unit": "raider_technical", "count": 3, "pos": [190, 96], "team": 1, "target": [90, 80]},
                {"type": "spawn", "unit": "raider_buggy", "count": 3, "pos": [192, 100], "team": 1, "target": [90, 80]}]},
        ],
    }
    doc = {
        "name": "Rail Depot",
        "width": W,
        "height": H,
        "theme": "winter",
        "cells": rle(grid),
        "playerStart": {"pos": [18, 70], "units": ["uaz", "uaz", "mtlb", "mtlb", "tractor_tank", "tractor_tank", "builder_truck"]},
        "defense": {"startEnergy": 200, "baseIncome": 0.5, "waves": []},
        "resourcePoints": [{"name": "Yard", "pos": [96, 62], "radius": 3.5, "income": 2}],
        "units": units,
        "buildings": buildings,
        "areas": areas,
        "paths": paths,
        "props": props,
        "lanes": [],
        "mission": mission,
    }
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "voronezh_s1.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, separators=(",", ":"))
    print(f"voronezh_s1: {len(buildings)} buildings, {len(units)} marauders")


if __name__ == "__main__":
    main()
