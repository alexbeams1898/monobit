#!/usr/bin/env python3
"""Render original vs Bayer variants, side-by-side, for each boss.

Three columns:
  original         — grayscale source (post-slice, post-portrait-ratio)
  B_bayer_shading  — Bayer dither + bottom spirit-fade (old look)
  B2_bayer_masked  — Bayer dither masked to the silhouette; per-sprite
                     bottom fade (Charon and Phlegyas get hard bottoms)

Output: art/_versions/<sprite>_compare.png and art/_versions/_all.png.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from tools.spritebake import config, stages  # noqa: F401
from tools.spritebake.loader import load_image, load_image_cell
from tools.spritebake.pipeline import Pipeline, StageSpec

MANIFEST = REPO / "games" / "rpg" / "sprites.toml"
OUT_DIR = REPO / "art" / "_versions"
OUT_DIR.mkdir(exist_ok=True)

SCALE = 12

# Sprites that should end at a hard edge (scythe shaft / flame base) rather
# than dissolving into pitch. Every other sprite opts into spirit_fade_bottom.
NO_FADE_IDENTS = {"boss_charon_data", "boss_phlegyas_data"}

SPIRIT_FADE = StageSpec("spirit_fade_bottom",
                        {"fade_ratio": 0.28, "top_keep": 0.90, "bottom_keep": 0.15})

# Old B column — Bayer with no masking. Leaves a dotted-grid field across
# the empty background because Bayer thresholds every pixel, including pitch.
B_BAYER_UNMASKED = [
    StageSpec("autocrop", {}),
    StageSpec("gaussian_blur", {"radius_ratio": 0.3}),
    StageSpec("aspect_fit", {"width": 0, "height": 0}),
    StageSpec("downscale", {"width": 0, "height": 0, "method": "lanczos"}),
    StageSpec("bayer_ordered_dither", {"matrix": 4, "threshold": 128}),
    SPIRIT_FADE,
]

# Masked-Bayer variants — stash a silhouette mask after downscale and AND
# it back in after dither so the background dot-grid disappears. Knobs:
#   sil_threshold  — how bright a pixel must be to count as inside the
#                    figure. Lower = more edge pixels survive.
#   matrix         — Bayer matrix size. 4 = 4x4 (current), 2 = 2x2 (coarser
#                    dots, may read better inside small silhouettes).
def bayer_masked(*, sil_threshold: int, matrix: int,
                 include_fade: bool) -> list[StageSpec]:
    out = [
        StageSpec("autocrop", {}),
        StageSpec("gaussian_blur", {"radius_ratio": 0.3}),
        StageSpec("aspect_fit", {"width": 0, "height": 0}),
        StageSpec("downscale", {"width": 0, "height": 0, "method": "lanczos"}),
        StageSpec("stash_silhouette_from_gray",
                  {"threshold": sil_threshold, "close_iterations": 1}),
        StageSpec("bayer_ordered_dither", {"matrix": matrix, "threshold": 128}),
        StageSpec("apply_stashed_silhouette", {}),
    ]
    if include_fade:
        out.append(SPIRIT_FADE)
    return out


COLUMNS = [
    "original",
    "B_bayer_shading",
    "B2_mask96_m4",
    "B3_mask64_m4",
    "B4_mask96_m2",
]


def bind_specs(specs, w, h):
    out = []
    for s in specs:
        kw = dict(s.kwargs)
        for k, v in kw.items():
            if v == 0 and k in ("width", "height"):
                kw[k] = w if k == "width" else h
        out.append(StageSpec(s.name, kw))
    return out


def load_source(spec, manifest):
    if spec.slice is not None:
        s = spec.slice
        return load_image_cell(
            spec.source, cols=s.cols, rows=s.rows, index=s.index,
            autocrop_to_figure=s.autocrop_to_figure,
            autocrop_threshold=s.autocrop_threshold,
            portrait_ratio=s.portrait_ratio,
        )
    return load_image(spec.source, portrait_ratio=spec.portrait_ratio)


def grayscale_to_image(src, target_w: int, target_h: int, scale: int) -> Image.Image:
    """Render the loader's grayscale source into a target-sized preview tile.

    Shows what the Bayer pipeline sees AFTER slice + portrait-ratio crop
    but BEFORE blur / downscale / dither. Fitted into the same
    target_w*scale × target_h*scale box so eye-balling is apples-to-apples
    with the baked columns.
    """
    gray = src.meta.get("gray")
    if gray is None:
        gray = np.where(src.pixels, 255.0, 0.0)
    arr = np.clip(gray, 0, 255).astype(np.uint8)
    im = Image.fromarray(arr, mode="L")
    box_w = target_w * scale
    box_h = target_h * scale
    src_w, src_h = im.size
    s = min(box_w / src_w, box_h / src_h)
    new_w = max(1, int(round(src_w * s)))
    new_h = max(1, int(round(src_h * s)))
    im = im.resize((new_w, new_h), Image.NEAREST)
    canvas = Image.new("L", (box_w, box_h), 0)
    canvas.paste(im, ((box_w - new_w) // 2, (box_h - new_h) // 2))
    return canvas.convert("RGB")


def run_bake(src, specs, width: int, height: int, scale: int) -> Image.Image:
    bound = bind_specs(specs, width, height)
    pipe = Pipeline(bound, name="bake")
    baked = pipe.run(src)
    return baked.to_upscaled_image(scale).convert("RGB")


def main():
    m = config.load_manifest(MANIFEST, repo_root=REPO)
    try:
        font = ImageFont.truetype("arial.ttf", 14)
    except OSError:
        font = ImageFont.load_default()

    pad = 8
    label_h = 20
    max_w = max(s.width for s in m.sprites) * SCALE
    max_h = max(s.height for s in m.sprites) * SCALE

    rows_h = max_h + pad * 2 + label_h
    row_w = pad + len(COLUMNS) * (max_w + pad)
    grid_h = pad + len(m.sprites) * (rows_h + pad)
    grid_w = row_w + 140

    grid = Image.new("RGB", (grid_w, grid_h), (20, 20, 20))
    g = ImageDraw.Draw(grid)

    header = Image.new("RGB", (grid_w, 22), (40, 40, 40))
    hd = ImageDraw.Draw(header)
    for i, cn in enumerate(COLUMNS):
        x = 140 + pad + i * (max_w + pad)
        hd.text((x, 4), cn, fill=(220, 220, 220), font=font)

    for si, spec in enumerate(m.sprites):
        src = load_source(spec, m)
        y = pad + si * (rows_h + pad)
        g.text((pad, y + max_h // 2 - 8),
               spec.ident.removesuffix("_data").removeprefix("boss_"),
               fill=(255, 255, 255), font=font)

        row_img = Image.new("RGB", (row_w, rows_h), (28, 28, 28))
        rd = ImageDraw.Draw(row_img)

        include_fade = spec.ident not in NO_FADE_IDENTS

        tiles: list[Image.Image] = [
            grayscale_to_image(src, spec.width, spec.height, SCALE),
            run_bake(src, B_BAYER_UNMASKED, spec.width, spec.height, SCALE),
            run_bake(src, bayer_masked(sil_threshold=96, matrix=4,
                                       include_fade=include_fade),
                     spec.width, spec.height, SCALE),
            run_bake(src, bayer_masked(sil_threshold=64, matrix=4,
                                       include_fade=include_fade),
                     spec.width, spec.height, SCALE),
            run_bake(src, bayer_masked(sil_threshold=96, matrix=2,
                                       include_fade=include_fade),
                     spec.width, spec.height, SCALE),
        ]

        for ci, tile in enumerate(tiles):
            gx = 140 + pad + ci * (max_w + pad)
            gy = y + (max_h - tile.height) // 2
            grid.paste(tile, (gx, gy))
            rx = pad + ci * (max_w + pad)
            ry = (max_h - tile.height) // 2
            row_img.paste(tile, (rx, ry))
            rd.text((rx, rows_h - label_h), COLUMNS[ci], fill=(220, 220, 220), font=font)

        per_path = OUT_DIR / f"{spec.ident.removesuffix('_data').removeprefix('boss_')}_compare.png"
        row_img.save(per_path)

    combined = Image.new("RGB", (grid_w, 22 + grid_h), (20, 20, 20))
    combined.paste(header, (0, 0))
    combined.paste(grid, (0, 22))
    combined.save(OUT_DIR / "_all.png")
    print(f"wrote {OUT_DIR}/_all.png and {len(m.sprites)} per-boss compares")


if __name__ == "__main__":
    main()