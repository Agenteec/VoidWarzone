"""Downloads the CC0 source assets used by the generators into tools/assets_src/ (gitignored).

python tools/fetch_assets.py
- Poly Haven textures (diffuse, 1k) -> assets_src/polyhaven/<name>/<name>_diff_1k.jpg
- Poly Haven models (glTF, 1k textures) -> assets_src/polyhaven/<name>/<name>.gltf (+ bin, textures)
- Kenney Particle Pack -> assets_src/kenney_particles/
Everything here is CC0 (polyhaven.com, kenney.nl); see data/models/CREDITS.md.
"""
import io
import json
import os
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "assets_src")
UA = {"User-Agent": "Mozilla/5.0"}

TEXTURES = [
    "snow_02", "snow_04", "asphalt_snow", "muddy_tracks", "rock_ground_02", "brown_mud_rocks_01",
    "burned_ground_01", "dry_mud_field_001", "rock_ground", "road_damaged",
    "concrete_panels", "precast_concrete_wall", "brick_wall_02", "rusty_corrugated_iron", "distressed_painted_planks",
    "rusty_metal_shutter", "concrete_debris", "damaged_plaster", "concrete_wall_008",
    "winter_leaves", "brown_mud_leaves_01", "asphalt_02", "dry_decay_leaves",
]

MODELS = [
    "barrel_03", "Barrel_01", "old_tyre", "metal_trash_can", "trashbag", "utility_box_01", "utility_box_02",
    "concrete_road_barrier_02", "covered_car", "wooden_military_crate", "old_military_crate", "propane_tank",
    "barrel_stove", "stone_fire_pit", "tree_stump_01", "dead_tree_trunk", "rock_07",
    "metal_jerrycan_green", "portable_generator", "water_manhole_cover",
]

KENNEY = "https://kenney.nl/media/pages/assets/particle-pack/f8fe0f8cb8-1677578741/kenney_particle-pack.zip"


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=120).read()


def files(name):
    return json.loads(get("https://api.polyhaven.com/files/" + name))


def texture(name):
    out = os.path.join(SRC, "polyhaven", name, name + "_diff_1k.jpg")
    if os.path.exists(out):
        return
    os.makedirs(os.path.dirname(out), exist_ok=True)
    info = files(name)
    with open(out, "wb") as f:
        f.write(get(info["Diffuse"]["1k"]["jpg"]["url"]))
    print("texture", name)


def model(name):
    root = os.path.join(SRC, "polyhaven", name)
    gltf = os.path.join(root, name + ".gltf")
    if os.path.exists(gltf):
        return
    entry = files(name)["gltf"]["1k"]["gltf"]
    os.makedirs(root, exist_ok=True)
    for rel, inc in entry.get("include", {}).items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(get(inc["url"]))
    with open(gltf, "wb") as f:
        f.write(get(entry["url"]))
    print("model", name)


def kenney():
    root = os.path.join(SRC, "kenney_particles")
    if os.path.isdir(os.path.join(root, "x")):
        return
    os.makedirs(root, exist_ok=True)
    zipfile.ZipFile(io.BytesIO(get(KENNEY))).extractall(os.path.join(root, "x"))
    print("kenney particles")


def main():
    for t in TEXTURES:
        texture(t)
    for m in MODELS:
        model(m)
    kenney()
    print("assets ready in", SRC)


if __name__ == "__main__":
    sys.exit(main())
