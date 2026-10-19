"""Particle sprite atlas from the Kenney Particle Pack (CC0); run tools/fetch_assets.py first.

python tools/gen_fx.py data
Writes data/fx/sprites.png: 8 x 8 cells (square for mips, 4 rows used) of 128 px, RGB = shading, A = coverage. The cell order is the
`Sprite` enum in src/client/render/Renderer.h - keep both in sync.
"""
import os
import sys

import numpy as np
from PIL import Image

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets_src", "kenney_particles", "x", "PNG (Black background)")
CELL = 128
FRAMES = [
    "smoke_01", "smoke_02", "smoke_04", "smoke_07", "fire_01", "fire_02", "flame_05", "flame_06",
    "muzzle_01", "muzzle_02", "muzzle_03", "muzzle_05", "trace_01", "trace_07", "flare_01", "circle_05",
    "star_06", "scorch_01", "scorch_03", "dirt_01", "dirt_03", "smoke_10", "spark_05", "light_01",
    "spark_01", "twirl_01", "smoke_09", "smoke_05",
]


def main():
    out = os.path.join(sys.argv[1] if len(sys.argv) > 1 else "data", "fx")
    os.makedirs(out, exist_ok=True)
    atlas = np.zeros((CELL * 8, CELL * 8, 4), dtype=np.uint8)
    for i, name in enumerate(FRAMES):
        img = Image.open(os.path.join(SRC, name + ".png")).convert("L").resize((CELL, CELL), Image.LANCZOS)
        a = np.asarray(img, dtype=np.float64) / 255.0
        y, x = (i // 8) * CELL, (i % 8) * CELL
        atlas[y:y + CELL, x:x + CELL, :3] = (np.sqrt(a)[..., None] * 255).astype(np.uint8)
        atlas[y:y + CELL, x:x + CELL, 3] = (a * 255).astype(np.uint8)
    Image.fromarray(atlas, "RGBA").save(os.path.join(out, "sprites.png"), optimize=True)
    print(len(FRAMES), "sprites written to", out)


if __name__ == "__main__":
    main()
