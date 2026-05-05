"""Bake the 8 figures from build/spritebake/unnamed.png through the
same threshold-only pipeline used by the player classes, at a portrait
size, and lay them out at 4x scale so we can pick a canonical pose for
the unburdened Pilgrim sprite.

Source: 4 columns × 2 rows, ~96x128 px per cell (auto-detected from
the lit-pixel bounds inside each cell).
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from tools.spritebake import stages  # register every stage
from tools.spritebake.core import SpriteData
from tools.spritebake.pipeline import Pipeline, StageSpec

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "build" / "spritebake" / "unnamed.png"
OUT = REPO / "build" / "spritebake" / "unnamed_preview.png"

# Target portrait size — pick something Heretic-L1-ish so the figure
# reads as "narrow/tall like a class portrait."
TGT_W, TGT_H = 28, 40

# Pipeline mirrors player_class_heretic from sprites.toml.
STAGES = [
    StageSpec(name="autocrop", kwargs={}),
    StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
    StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.3}),
    StageSpec(name="aspect_fit", kwargs={"width": TGT_W, "height": TGT_H}),
    StageSpec(name="downscale", kwargs={"width": TGT_W, "height": TGT_H, "method": "lanczos"}),
    StageSpec(name="threshold_fixed", kwargs={"value": 80}),
    StageSpec(name="morph_open_speckle", kwargs={"min_neighbors": 1}),
]

GRID_COLS = 4
GRID_ROWS = 2


def load_source_grayscale(path: Path) -> np.ndarray:
    img = Image.open(path).convert("L")
    return np.array(img)


def slice_cells(arr: np.ndarray, cols: int, rows: int):
    """Slice the source PNG into cols×rows equal cells. Returns a list
    of (row, col, cell_array) tuples in reading order."""
    h, w = arr.shape
    cell_w = w // cols
    cell_h = h // rows
    cells = []
    for r in range(rows):
        for c in range(cols):
            x0 = c * cell_w
            y0 = r * cell_h
            x1 = x0 + cell_w
            y1 = y0 + cell_h
            cells.append((r, c, arr[y0:y1, x0:x1]))
    return cells


def bake_cell(cell_arr: np.ndarray) -> SpriteData:
    """Run the cell through the player_class_heretic-equivalent pipeline."""
    # Autocrop expects light-on-dark "lit" pixels; the source PNG has
    # dark figures on a light background, so invert before feeding.
    inverted = 255 - cell_arr
    sprite = SpriteData(
        pixels=(inverted >= 128),  # initial mask, pipeline will refine
        meta={"gray": inverted.astype(np.float32)},
    )
    pipeline = Pipeline(STAGES, name="unnamed_preview")
    return pipeline.run(sprite)


def to_image(sprite: SpriteData, invert: bool = False) -> Image.Image:
    """Convert SpriteData (boolean pixels) to a PIL L image. Default:
    lit pixels → white, matching in-game framebuffer convention."""
    arr = sprite.pixels
    h, w = arr.shape
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if arr[y, x] else 0
    if invert:
        img = Image.eval(img, lambda v: 255 - v)
    return img


def main():
    src_arr = load_source_grayscale(SRC)
    print(f"source: {SRC} ({src_arr.shape[1]}x{src_arr.shape[0]})")
    cells = slice_cells(src_arr, GRID_COLS, GRID_ROWS)
    print(f"sliced into {len(cells)} cells, each "
          f"{src_arr.shape[1] // GRID_COLS}x{src_arr.shape[0] // GRID_ROWS}")

    # Bake each cell.
    baked = []
    for r, c, cell in cells:
        try:
            sprite = bake_cell(cell)
            baked.append((r, c, sprite))
        except Exception as e:
            print(f"  cell ({r},{c}) failed: {e}")
            baked.append((r, c, None))

    # Lay out the result at 4x scale on a dark canvas (matches in-game
    # white-on-black rendering convention).
    SCALE = 4
    PAD = 12
    LABEL_H = 18
    cell_w = TGT_W * SCALE
    cell_h = TGT_H * SCALE + LABEL_H
    img_w = PAD + GRID_COLS * (cell_w + PAD)
    img_h = PAD + GRID_ROWS * (cell_h + PAD)

    canvas = Image.new("L", (img_w, img_h), 0)
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("arial.ttf", 12)
    except OSError:
        font = ImageFont.load_default()

    for (r, c, sprite) in baked:
        x0 = PAD + c * (cell_w + PAD)
        y0 = PAD + r * (cell_h + PAD)
        if sprite is None:
            draw.text((x0 + 4, y0 + 4), "FAILED", fill=200, font=font)
            continue
        si = to_image(sprite, invert=False)
        scaled = si.resize((TGT_W * SCALE, TGT_H * SCALE), Image.NEAREST)
        canvas.paste(scaled, (x0, y0))
        # Pose label: row+col (so 0,0 = top-left, 1,3 = bottom-right)
        draw.text((x0, y0 + TGT_H * SCALE + 2), f"r{r}c{c}", fill=200, font=font)

    canvas.save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
