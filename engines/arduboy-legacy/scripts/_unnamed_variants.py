"""Try a handful of pipeline / size combos on the same source pose
(top-left of unnamed.png) so we can pick which combination reads
cleanest for the unburdened Pilgrim sprite. Lays the variants out
side-by-side with their settings labeled.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from tools.spritebake import stages  # register stages
from tools.spritebake.core import SpriteData
from tools.spritebake.pipeline import Pipeline, StageSpec

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "build" / "spritebake" / "unnamed.png"
OUT = REPO / "build" / "spritebake" / "unnamed_variants.png"

# Canonical pose: bottom-row, leftmost (r1c0). Looking down, somber,
# docile — willing to be made a puppet. Locked-in unburdened-Pilgrim
# sprite.
GRID_COLS, GRID_ROWS = 4, 2
TARGET_R, TARGET_C = 1, 0


def slice_cell(arr: np.ndarray) -> np.ndarray:
    h, w = arr.shape
    cw, ch = w // GRID_COLS, h // GRID_ROWS
    x0, y0 = TARGET_C * cw, TARGET_R * ch
    return arr[y0:y0 + ch, x0:x0 + cw]


def bake(cell: np.ndarray, w: int, h: int, stage_specs) -> SpriteData:
    inverted = 255 - cell
    sprite = SpriteData(
        pixels=(inverted >= 128),
        meta={"gray": inverted.astype(np.float32)},
    )
    return Pipeline(stage_specs, name="variant").run(sprite)


def to_image(sprite: SpriteData) -> Image.Image:
    arr = sprite.pixels
    h, w = arr.shape
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if arr[y, x] else 0
    return img


def heretic_pipeline(w: int, h: int, blur: float = 0.3, thresh: int = 80):
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": blur}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="threshold_fixed", kwargs={"value": thresh}),
        StageSpec(name="morph_open_speckle", kwargs={"min_neighbors": 1}),
    ]


def edge_preserve_pipeline(w: int, h: int):
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.3}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="sobel_edges", kwargs={}),
        StageSpec(name="edge_preserving_dither", kwargs={"edge_threshold": 60, "interior_threshold": 140}),
        StageSpec(name="morph_close_holes", kwargs={}),
    ]


def silhouette_pipeline(w: int, h: int, sil_thresh: int = 80):
    """Outline-and-fill: extracts the figure as a SOLID silhouette,
    losing interior detail but giving a clean edge that doesn't get
    stair-stepped by threshold. Best for line-art sources where the
    interior detail (anatomical shading) is the source of blockiness."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.4}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="outline_and_fill", kwargs={"silhouette_threshold": sil_thresh, "fill": True}),
        StageSpec(name="morph_close_holes", kwargs={}),
    ]


def dither_pipeline(w: int, h: int, dith_thresh: int = 128):
    """Floyd-Steinberg: trades stair-step edges for a stippled texture
    inside the figure. Less blocky, more painterly."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.3}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="floyd_steinberg", kwargs={"threshold": dith_thresh}),
        StageSpec(name="morph_close_holes", kwargs={}),
    ]


def heretic_smoothed(w: int, h: int, blur: float, thresh: int):
    """Heretic threshold pipeline + extra morph_close_holes after the
    threshold step to fill 1-pixel gaps caused by stair-step edge."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": blur}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="threshold_fixed", kwargs={"value": thresh}),
        StageSpec(name="morph_close_holes", kwargs={}),
        StageSpec(name="morph_open_speckle", kwargs={"min_neighbors": 1}),
    ]


def edge_outline_pipeline(w: int, h: int, edge_thr: int):
    """Sobel edge detection on the source, threshold the gradient
    magnitude — keeps the outline geometry rather than the bright fill.
    Preserves the figure's actual outline shape."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.4}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="sobel_edges", kwargs={}),
        StageSpec(name="threshold_edge_biased", kwargs={"base": 255, "edge_weight": 1.0}),
        StageSpec(name="morph_close_holes", kwargs={}),
    ]


def outline_fill_pipeline(w: int, h: int, sil_thr: int):
    """outline_and_fill: traces the outer outline at sil_thr, fills
    interior. Solid silhouette but with the figure's TRUE outline
    geometry, not a halo-bloated version."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.4}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="outline_and_fill", kwargs={"silhouette_threshold": sil_thr, "fill": True}),
        StageSpec(name="morph_close_holes", kwargs={}),
    ]


def outline_only_pipeline(w: int, h: int, sil_thr: int):
    """outline_and_fill with fill=False — just the outline strokes.
    Reads as a line-art rendering of the figure."""
    return [
        StageSpec(name="autocrop", kwargs={}),
        StageSpec(name="pad", kwargs={"top": 10, "bottom": 4, "left": 4, "right": 4}),
        StageSpec(name="gaussian_blur", kwargs={"radius_ratio": 0.4}),
        StageSpec(name="aspect_fit", kwargs={"width": w, "height": h}),
        StageSpec(name="downscale", kwargs={"width": w, "height": h, "method": "lanczos"}),
        StageSpec(name="outline_and_fill", kwargs={"silhouette_threshold": sil_thr, "fill": False}),
    ]


# Outline-preserving variants. Goal: capture the figure's actual outline
# (head curve, shoulder slope, gun grip shape) rather than a halo-bloated
# blob. Three approaches: Sobel edge magnitude, silhouette outline+fill,
# and pure outline (no fill).
VARIANTS = [
    ("40x52 sobel-thr180",       40, 52, edge_outline_pipeline(40, 52, 180)),
    ("40x52 sobel-thr140",       40, 52, edge_outline_pipeline(40, 52, 140)),
    ("40x52 outlinefill-100",    40, 52, outline_fill_pipeline(40, 52, 100)),
    ("40x52 outlinefill-140",    40, 52, outline_fill_pipeline(40, 52, 140)),
    ("46x60 outlinefill-100",    46, 60, outline_fill_pipeline(46, 60, 100)),
    ("46x60 outlinefill-140",    46, 60, outline_fill_pipeline(46, 60, 140)),
    ("46x60 outline-only-100",   46, 60, outline_only_pipeline(46, 60, 100)),
    ("46x60 outline-only-140",   46, 60, outline_only_pipeline(46, 60, 140)),
]


def main():
    arr = np.array(Image.open(SRC).convert("L"))
    cell = slice_cell(arr)

    SCALE = 4
    PAD = 14
    LABEL_H = 22
    # Find max scaled size for uniform cell sizing
    max_sw = max(v[1] for v in VARIANTS) * SCALE
    max_sh = max(v[2] for v in VARIANTS) * SCALE
    cell_w = max_sw + PAD
    cell_h = max_sh + LABEL_H + PAD

    cols = 4
    rows = (len(VARIANTS) + cols - 1) // cols
    img_w = PAD + cols * cell_w
    img_h = PAD + rows * cell_h

    canvas = Image.new("L", (img_w, img_h), 0)
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("arial.ttf", 11)
    except OSError:
        font = ImageFont.load_default()

    for i, (label, w, h, specs) in enumerate(VARIANTS):
        try:
            sprite = bake(cell, w, h, specs)
            img = to_image(sprite)
            scaled = img.resize((w * SCALE, h * SCALE), Image.NEAREST)
        except Exception as e:
            print(f"  {label} failed: {e}")
            continue
        col = i % cols
        row = i // cols
        x0 = PAD + col * cell_w + (max_sw - w * SCALE) // 2
        y0 = PAD + row * cell_h
        canvas.paste(scaled, (x0, y0))
        draw.text((PAD + col * cell_w, y0 + max_sh + 4), label, fill=220, font=font)

    canvas.save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
