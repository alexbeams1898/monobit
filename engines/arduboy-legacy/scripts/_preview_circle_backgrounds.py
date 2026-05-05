#!/usr/bin/env python3
"""Debug helper: re-run the circle-background conversion and dump each baked
tile as a visible PNG (upscaled 4x) for eyeball review.

Usage:
    python scripts/_preview_circle_backgrounds.py

Writes to art/_preview/tile_0_LIMBO.png ... tile_8_TREACHERY.png.
Also writes a combined _preview/atlas.png showing all nine side-by-side.
"""
from __future__ import annotations
from pathlib import Path
from PIL import Image
import sys

# Pull the transform functions from the real converter so previews match ship.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from convert_circle_backgrounds import (  # noqa: E402
    split_atlas, box_threshold_despeckle, checker_mask, CIRCLES, TILE_W, TILE_H,
)

SRC = Path(__file__).resolve().parents[1] / "art" / "circlebackgroundsinorder.png"
OUT = Path(__file__).resolve().parents[1] / "art" / "_preview"
SCALE = 4  # upscale so humans can actually see the stippling


def main() -> int:
    if not SRC.exists():
        sys.stderr.write(f"source atlas not found: {SRC}\n")
        return 1
    OUT.mkdir(exist_ok=True, parents=True)

    atlas = Image.open(SRC).convert("L")
    tiles_L = split_atlas(atlas)

    baked = []
    for i, tile in enumerate(tiles_L):
        d = box_threshold_despeckle(tile)  # 1-bit, forms preserved
        m = checker_mask(d.copy())         # half-density stipple of those forms
        # Render as 8-bit gray so PNG viewers display sanely.
        rgb = m.convert("L")
        up = rgb.resize((TILE_W * SCALE, TILE_H * SCALE), Image.NEAREST)
        path = OUT / f"tile_{i}_{CIRCLES[i]}.png"
        up.save(path)
        baked.append(up)
        print(f"wrote {path}")

    # Side-by-side atlas: 3 cols, 3 rows, small gap.
    gap = 8
    w = TILE_W * SCALE
    h = TILE_H * SCALE
    combined = Image.new("L", (3 * w + 4 * gap, 3 * h + 4 * gap), 64)
    for i, img in enumerate(baked):
        row, col = divmod(i, 3)
        x = gap + col * (w + gap)
        y = gap + row * (h + gap)
        combined.paste(img, (x, y))
    combined.save(OUT / "atlas.png")
    print(f"wrote {OUT / 'atlas.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
