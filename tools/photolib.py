"""Seamless photo textures (Poly Haven, CC0) for the generators; run tools/fetch_assets.py first."""
import os

import numpy as np
from PIL import Image

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets_src", "polyhaven")


def photo(name, size=256, repeat=1, sat=1.0, bright=1.0, tint=None):
    """RGB float array (size, size, 3) in 0..255, the source tiled `repeat` times per side."""
    img = Image.open(os.path.join(SRC, name, name + "_diff_1k.jpg")).convert("RGB")
    tile = img.resize((size // repeat, size // repeat), Image.LANCZOS)
    a = np.asarray(tile, dtype=np.float64)
    a = np.tile(a, (repeat, repeat, 1))
    grey = a.mean(-1, keepdims=True)
    a = grey + (a - grey) * sat
    if tint is not None:
        a = a * (np.array(tint, dtype=np.float64) / 255.0)
    return np.clip(a * bright, 0, 255)


def level(a, mean):
    """Shifts the average brightness to `mean` keeping the detail."""
    return np.clip(a + (mean - a.mean()), 0, 255)
