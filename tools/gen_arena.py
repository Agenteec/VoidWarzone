import json
import sys

W = H = 104
LO, HI = 16, 88
MID = 52
GATE = 3


def blocked(x, y):
    if x < 2 or y < 2 or x >= W - 2 or y >= H - 2:
        return True
    if LO <= x < HI and LO <= y < HI:
        for px, py in ((28, 28), (76, 28), (28, 76), (76, 76)):
            if abs(x + 0.5 - px) < 2.5 and abs(y + 0.5 - py) < 2.5:
                return True
        return False
    return not (abs(x + 0.5 - MID) < GATE or abs(y + 0.5 - MID) < GATE) or (
        (x < LO or x >= HI) and (y < LO or y >= HI))


walls = []
for y in range(H):
    x = 0
    while x < W:
        if blocked(x, y):
            s = x
            while x < W and blocked(x, y):
                x += 1
            walls.append([s, y, x - s, 1])
        else:
            x += 1

lanes = [
    {"name": "north", "points": [[MID, MID], [MID, LO - 2], [MID, 5]]},
    {"name": "south", "points": [[MID, MID], [MID, HI + 2], [MID, H - 5]]},
    {"name": "west", "points": [[MID, MID], [LO - 2, MID], [5, MID]]},
    {"name": "east", "points": [[MID, MID], [HI + 2, MID], [W - 5, MID]]},
]

waves = []
for i in range(15):
    n = i + 1
    groups = [{"unit": "void_melee", "count": 2 + n // 2, "lanes": "all"}]
    if n >= 2:
        groups.append({"unit": "void_ranged", "count": 1 + n // 3, "lanes": "all"})
    if n >= 4:
        groups.append({"unit": "void_brute", "count": n // 4, "lanes": "all"})
    wave = {"groups": groups}
    if n == 8:
        wave["boss"] = {"unit": "void_warden", "lane": "north"}
        wave["delay"] = 55
    if n == 15:
        wave["boss"] = {"unit": "void_titan", "lane": "south"}
        wave["delay"] = 60
    waves.append(wave)

arena = {
    "name": "Void Arena",
    "width": W,
    "height": H,
    "walls": walls,
    "lanes": lanes,
    "units": [{"unit": "core", "role": "core", "team": 0, "pos": [MID, MID]}],
    "playerStart": {"pos": [MID, MID + 7], "units": ["tank", "scout", "scout"]},
    "resourcePoints": [
        {"name": "A", "pos": [38, 38], "radius": 3.0, "income": 2},
        {"name": "B", "pos": [66, 38], "radius": 3.0, "income": 2},
        {"name": "C", "pos": [38, 66], "radius": 3.0, "income": 2},
        {"name": "D", "pos": [66, 66], "radius": 3.0, "income": 2},
    ],
    "defense": {"firstWaveDelay": 60, "waveInterval": 40, "baseIncome": 3, "startEnergy": 400, "waves": waves},
}

with open(sys.argv[1], "w", newline="\n") as f:
    json.dump(arena, f, separators=(",", ":"))
    f.write("\n")
