"""Chapter 1 mission specs (Voronezh) for tools/osm_import.py.

Usage: python tools/gen_campaign.py && python tools/osm_import.py tools/maps/voronezh_m1.json (m2, m3)
Geometry comes from the shared Shilovo OSM extract; this script only writes the gameplay blocks.
"""
import json
import os

BASE = {
    "origin": [51.57685, 39.1162],
    "cell": 6,
    "width": 512,
    "height": 352,
    "osm": "voronezh_shilovo.osm.json",
    "ruins": 0.12,
    "ruinZones": [{"pos": [378, 282], "radius": 60, "chance": 0.85}],
    "garages": [{"pos": [205, 232], "rows": 3, "length": 18, "angle": -35}, {"pos": [300, 105], "rows": 2, "length": 16, "angle": -35}],
}


def unit(u, pos, team=1, tag=None, role=None):
    d = {"unit": u, "team": team, "pos": pos}
    if tag:
        d["tag"] = tag
    if role:
        d["role"] = role
    return d


def say(text):
    return {"type": "message", "text": text}


def spawn(u, count, pos, target=None, team=1, tag=None, player=False, target_tag=None):
    d = {"type": "spawn", "unit": u, "count": count, "pos": pos, "team": team}
    if target:
        d["target"] = target
    if target_tag:
        d["targetTag"] = target_tag
    if tag:
        d["tag"] = tag
    if player:
        d["player"] = True
    return d


M1 = {
    "name": "Cold Start",
    "theme": "winter",
    "playerStart": {"pos": [30, 100], "units": ["kshm", "builder_truck", "builder_truck", "uaz", "uaz", "mtlb"]},
    "defense": {"startEnergy": 450, "baseIncome": 1.5, "waves": []},
    "resourcePoints": [{"name": "A", "pos": [150, 108], "radius": 3.5, "income": 2}, {"name": "B", "pos": [232, 82], "radius": 3.5, "income": 3}],
    "units": [
        unit("raider_bunker", [140, 150], tag="outpost"), unit("raider_bunker", [151, 160], tag="outpost"),
        unit("raider_technical", [147, 148], tag="outpost"), unit("raider_buggy", [138, 160], tag="outpost"),
        unit("raider_camp", [228, 252], tag="camp"), unit("raider_bunker", [218, 246], tag="camp"), unit("raider_bunker", [234, 242], tag="camp"),
        unit("raider_mortar", [232, 258], tag="camp"), unit("raider_gatling", [222, 256], tag="camp"), unit("raider_rocket", [238, 250], tag="camp"),
    ],
    "mission": {
        "title": "Cold Start",
        "briefing": "Shilovo, southern edge of Voronezh. Third winter after the Breach. A marauder gang has taken the gas station and the garage "
                    "cooperative by the main road and is robbing every convoy that tries to reach the city. Take your column through the blocks, "
                    "clear the outpost and burn their camp.",
        "objectives": [
            {"id": "blocks", "text": "Reach the Shilovo blocks", "type": "reach", "pos": [105, 112], "radius": 12},
            {"id": "outpost", "text": "Clear the marauder outpost at the gas station", "type": "destroy", "tag": "outpost", "hidden": True},
            {"id": "camp", "text": "Destroy the marauder camp by the garages", "type": "destroy", "tag": "camp", "hidden": True},
            {"id": "alive", "text": "Keep the column alive", "type": "survive", "time": 99999, "optional": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("Column, this is Command. Deploy the KShM into a field HQ (Y), build a workshop, then move east into the blocks."),]},
            {"when": "objective", "objective": "blocks", "do": [
                say("Smoke over the gas station. Marauders. They've dug in with sandbags - take them out."),
                {"type": "reveal", "objective": "outpost"}]},
            {"when": "time", "time": 75, "do": [spawn("raider_buggy", 3, [200, 120], [105, 112])]},
            {"when": "destroyed", "tag": "outpost", "do": [
                say("Outpost is clear. Fuel trucks are yours. Their main camp is in the garages south-east of the road."),
                {"type": "energy", "amount": 250}, {"type": "reveal", "objective": "camp"},
                spawn("mtlb", 2, [40, 110], player=True), say("Two MT-LBs from the depot are joining you.")]},
            {"when": "area", "pos": [205, 232], "radius": 22, "do": [
                say("Movement in the garages! Technicals coming from the field."),
                spawn("raider_technical", 2, [270, 270], [210, 236]), spawn("raider_buggy", 3, [260, 300], [210, 236])]},
            {"when": "destroyed", "tag": "camp", "do": [say("Camp is burning. The road to the city is open. Good work, column.")]},
        ],
    },
}

M2 = {
    "name": "Holdout",
    "theme": "winter",
    "playerStart": {"pos": [118, 132], "units": ["builder_truck", "builder_truck", "uaz", "mtlb", "mtlb"]},
    "defense": {"startEnergy": 600, "baseIncome": 2.5, "waves": []},
    "resourcePoints": [{"name": "A", "pos": [150, 108], "radius": 3.5, "income": 3}, {"name": "B", "pos": [90, 170], "radius": 3.5, "income": 3}],
    "units": [
        {"unit": "hq", "team": 0, "owner": 0, "pos": [104, 128], "tag": "hq"},
        {"unit": "gun_tower", "team": 0, "owner": 0, "pos": [124, 124]}, {"unit": "workshop", "team": 0, "owner": 0, "pos": [96, 140]},
        unit("raider_camp", [300, 150], tag="nest"), unit("raider_bunker", [290, 140], tag="nest"), unit("raider_bunker", [306, 162], tag="nest"),
    ],
    "mission": {
        "title": "Holdout",
        "briefing": "We set up a forward HQ in the Shilovo blocks. The marauders want it back and are gathering every truck they have. "
                    "Hold the HQ for six minutes until the convoy from the city arrives. Engineers can build towers and walls.",
        "objectives": [
            {"id": "hold", "text": "Hold out for 6 minutes", "type": "survive", "time": 360},
            {"id": "hq", "text": "The HQ must survive", "type": "protect", "tag": "hq"},
            {"id": "nest", "text": "Optional: destroy the marauder staging camp", "type": "destroy", "tag": "nest", "optional": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("HQ is up. Build defences, the first raid is a minute out.")]},
            {"when": "time", "time": 60, "repeat": 45, "times": 7, "do": [
                spawn("raider_buggy", 5, [260, 120], [110, 130], target_tag="hq"), spawn("raider_technical", 3, [270, 190], [110, 135], target_tag="hq"),
                spawn("raider_mortar", 1, [265, 160], [112, 130], target_tag="hq")]},
            {"when": "time", "time": 150, "repeat": 60, "times": 4, "do": [
                say("Mortar trucks on the main road!"), spawn("raider_mortar", 3, [250, 180], [112, 132], target_tag="hq"), spawn("raider_rocket", 1, [255, 175], [112, 132], target_tag="hq"),
                spawn("raider_technical", 3, [240, 90], [110, 125], target_tag="hq")]},
            {"when": "time", "time": 240, "repeat": 40, "times": 3, "do": [
                say("Their whole gang is coming! Drones in the air!"), spawn("raider_technical", 5, [280, 150], [106, 130], target_tag="hq"), spawn("raider_buggy", 5, [250, 230], [106, 130], target_tag="hq"),
                spawn("raider_drone", 3, [240, 140], [106, 130], target_tag="hq")]},
            {"when": "time", "time": 300, "do": [say("Convoy is a minute away. Hold on!")]},
            {"when": "objective", "objective": "hold", "do": [say("The convoy is here. Shilovo is ours.")]},
        ],
    },
}

M3 = {
    "name": "Glow over the Plant",
    "theme": "winter",
    "playerStart": {"pos": [180, 200], "units": ["kshm", "builder_truck", "uaz", "mtlb", "mtlb", "mtlb"]},
    "defense": {"startEnergy": 500, "baseIncome": 2.0, "waves": []},
    "resourcePoints": [{"name": "A", "pos": [260, 300], "radius": 3.5, "income": 3}, {"name": "B", "pos": [290, 150], "radius": 4, "income": 4}],
    "units": [
        unit("portal", [378, 282], tag="portal", role="portal"),
        unit("void_pylon", [366, 272], tag="pylon"), unit("void_pylon", [392, 274], tag="pylon"), unit("void_pylon", [380, 298], tag="pylon"),
        unit("void_tower", [345, 262]), unit("void_tower", [352, 300]), unit("void_spire" if False else "void_tower", [400, 255]),
        unit("void_citadel", [360, 290]),
        unit("raider_bunker", [300, 245], tag="wreck"), unit("raider_technical", [296, 250], tag="wreck"),
    ],
    "mission": {
        "title": "Glow over the Plant",
        "briefing": "Scouts saw a violet glow over the ruins of the unfinished nuclear heating plant south of the city - right where the Breach "
                    "began. The marauders who went there did not come back. Find out what is there.",
        "objectives": [
            {"id": "outskirts", "text": "Reach the plant outskirts", "type": "reach", "pos": [305, 250], "radius": 14},
            {"id": "pylons", "text": "Destroy the crystal pylons", "type": "destroy", "tag": "pylon", "hidden": True},
            {"id": "portal", "text": "Destroy the rift", "type": "destroy", "tag": "portal", "hidden": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("Keep radio silence. Something is jamming everything near the plant.")]},
            {"when": "objective", "objective": "outskirts", "do": [
                say("Those are not marauders... The trucks are overgrown with crystals. Command, are you seeing this?"),
                say("A rift. It's feeding on the pylons around it. Destroy the pylons first, then the rift itself."),
                {"type": "reveal", "objective": "pylons"}, {"type": "reveal", "objective": "portal"},
                spawn("void_melee", 4, [360, 270], [305, 250])]},
            {"when": "time", "time": 120, "repeat": 45, "times": 12, "do": [spawn("void_melee", 3, [372, 290], [300, 250]), spawn("void_ranged", 1, [372, 290], [300, 250])]},
            {"when": "destroyed", "tag": "pylon", "do": [say("The pylons are down - the rift is unstable. Finish it!")]},
            {"when": "destroyed", "tag": "portal", "do": [say("The glow is fading... For now. Command, the war just changed.")]},
        ],
    },
}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    for name, block in (("voronezh_m1", M1), ("voronezh_m2", M2), ("voronezh_m3", M3)):
        spec = dict(BASE)
        spec["output"] = f"data/maps/{name}.json"
        spec["map"] = block
        with open(os.path.join(here, "maps", name + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(spec, f, ensure_ascii=False, indent=1)
    print("mission specs written")


if __name__ == "__main__":
    main()
