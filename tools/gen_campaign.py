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
        "briefing": "Shilovo, southern edge of Voronezh. Third winter after the Breach. Our column has come from the north with the last mobile HQ "
                    "of the colony. The empty panel blocks of Shilovo are the best place for a new base - but a marauder gang holds the gas station "
                    "and the garage cooperative by the main road and robs every convoy. Set up the base in the blocks, then drive the gang out.",
        "objectives": [
            {"id": "hq", "text": "Deploy the KShM as a field HQ in the Shilovo blocks", "type": "build", "unit": "hq", "pos": [105, 112], "radius": 18},
            {"id": "workshop", "text": "Build a workshop", "type": "build", "unit": "workshop", "hidden": True},
            {"id": "outpost", "text": "Clear the marauder outpost at the gas station", "type": "destroy", "tag": "outpost", "hidden": True},
            {"id": "camp", "text": "Destroy the marauder camp by the garages", "type": "destroy", "tag": "camp", "hidden": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("Column, this is Command. Drive east into the Shilovo blocks and deploy the KShM there (select it, press Y). That is our new home.")]},
            {"when": "objective", "objective": "hq", "do": [
                say("The HQ is up. Now a workshop - the crane trucks can build it. We need trucks and armour."),
                {"type": "reveal", "objective": "workshop"}, {"type": "energy", "amount": 100}]},
            {"when": "objective", "objective": "workshop", "do": [
                say("Smoke over the gas station east of the blocks. Marauders, dug in behind sandbags. Take them out."),
                {"type": "reveal", "objective": "outpost"}]},
            {"when": "time", "time": 150, "do": [say("Buggies on the main road - they have noticed us."),
                                                 spawn("raider_buggy", 3, [200, 120], [105, 112], target_tag="hq")]},
            {"when": "destroyed", "tag": "outpost", "do": [
                say("Outpost is clear and the fuel trucks are ours. Their main camp is in the garages south-east of the road."),
                {"type": "energy", "amount": 250}, {"type": "reveal", "objective": "camp"},
                spawn("mtlb", 2, [40, 110], player=True), say("Two MT-LBs from the old depot are joining you.")]},
            {"when": "area", "pos": [205, 232], "radius": 22, "do": [
                say("Movement in the garages! Technicals coming from the field."),
                spawn("raider_technical", 2, [270, 270], [210, 236]), spawn("raider_buggy", 3, [260, 300], [210, 236])]},
            {"when": "destroyed", "tag": "camp", "do": [say("The camp is burning. Shilovo is ours - dig in, they will come back for it.")]},
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
        "briefing": "Two days later. The gang that lost the garages ran to its staging camp in the east and called in every crew along the highway. "
                    "They will hit our base in the Shilovo blocks. A convoy from the city is on its way with fuel and people - hold the base "
                    "until it arrives. Crane trucks can build towers and walls.",
        "objectives": [
            {"id": "hold", "text": "Hold out for 6 minutes", "type": "survive", "time": 360},
            {"id": "hq", "text": "The HQ must survive", "type": "protect", "tag": "hq"},
            {"id": "nest", "text": "Optional: destroy the marauder staging camp", "type": "destroy", "tag": "nest", "optional": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("Command here. Build defences around the HQ - the first raid is a minute out.")]},
            {"when": "time", "time": 60, "repeat": 45, "times": 7, "do": [
                spawn("raider_buggy", 5, [260, 120], [110, 130], target_tag="hq"), spawn("raider_technical", 3, [270, 190], [110, 135], target_tag="hq"),
                spawn("raider_mortar", 1, [265, 160], [112, 130], target_tag="hq")]},
            {"when": "time", "time": 150, "repeat": 60, "times": 4, "do": [
                say("Mortar trucks on the main road!"), spawn("raider_mortar", 3, [250, 180], [112, 132], target_tag="hq"),
                spawn("raider_rocket", 1, [255, 175], [112, 132], target_tag="hq"), spawn("raider_technical", 3, [240, 90], [110, 125], target_tag="hq")]},
            {"when": "time", "time": 240, "repeat": 40, "times": 3, "do": [
                say("Their whole gang is coming! Drones in the air!"), spawn("raider_technical", 5, [280, 150], [106, 130], target_tag="hq"),
                spawn("raider_buggy", 5, [250, 230], [106, 130], target_tag="hq"), spawn("raider_drone", 3, [240, 140], [106, 130], target_tag="hq")]},
            {"when": "time", "time": 300, "do": [say("The convoy is a minute away. Hold on!")]},
            {"when": "destroyed", "tag": "nest", "do": [say("Their staging camp is gone. The raids will thin out.")]},
            {"when": "objective", "objective": "hold", "do": [
                say("The convoy is here. The drivers say the gangs get their fuel from the old rail depot east of the city.")]},
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
        "briefing": "Since the rail depot burned, the gangs have gone quiet. The few we caught were not running from us - they fled from the south, "
                    "from the ruins of the unfinished nuclear heating plant, right where the Breach began. Scouts saw a violet glow over the cooling "
                    "towers. Take the base's forces south and find out what is there.",
        "objectives": [
            {"id": "outskirts", "text": "Reach the plant outskirts", "type": "reach", "pos": [305, 250], "radius": 14},
            {"id": "pylons", "text": "Destroy the crystal pylons", "type": "destroy", "tag": "pylon", "hidden": True},
            {"id": "portal", "text": "Destroy the rift", "type": "destroy", "tag": "portal", "hidden": True},
        ],
        "triggers": [
            {"when": "start", "do": [say("Keep radio silence south of the blocks. Something is jamming everything near the plant.")]},
            {"when": "objective", "objective": "outskirts", "do": [
                say("Those are not marauders... The trucks here are overgrown with crystals. Command, are you seeing this?"),
                say("A rift. It is feeding on the pylons around it. Destroy the pylons first, then the rift itself."),
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
