#!/usr/bin/env python3
"""Render each baked sprite next to its source for per-sprite tuning.

Output: art/_side_by_side.png
Each row: source (thumbnail) | name | current bake (target resolution × 16).
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))

from tools.spritebake import config
from tools.spritebake import stages  # register
from tools.spritebake.loader import load_image, load_image_cell

MANIFEST = REPO / "games" / "rpg" / "sprites.toml"
OUT = REPO / "art" / "_side_by_side.png"

SRC_THUMB = 160  # px side for source thumbnail
SCALE = 16       # target-sprite upscale

def render_sprite(spec: config.SpriteSpec, manifest: config.Manifest):
    if spec.slice is not None:
        s = spec.slice
        src = load_image_cell(spec.source, cols=s.cols, rows=s.rows, index=s.index,
                              trim_top_ratio=s.trim_top_ratio,
                              trim_bottom_ratio=s.trim_bottom_ratio,
                              trim_left_ratio=s.trim_left_ratio,
                              trim_right_ratio=s.trim_right_ratio,
                              autocrop_to_figure=s.autocrop_to_figure,
                              autocrop_threshold=s.autocrop_threshold,
                              portrait_ratio=s.portrait_ratio)
    else:
        src = load_image(spec.source, portrait_ratio=spec.portrait_ratio)
    pipeline = spec.resolve_pipeline(manifest.pipelines)
    baked = pipeline.run(src)
    return src, baked

def main():
    m = config.load_manifest(MANIFEST, repo_root=REPO)

    try:
        font = ImageFont.truetype("arial.ttf", 14)
    except OSError:
        font = ImageFont.load_default()

    rows = []
    for spec in m.sprites:
        src, baked = render_sprite(spec, m)
        rows.append((spec, src, baked))

    pad = 10
    row_h = SRC_THUMB + pad
    for spec, src, baked in rows:
        scaled_h = baked.height * SCALE + pad * 2
        row_h = max(row_h, scaled_h)
    total_h = row_h * len(rows) + pad

    # Width: source_thumb + label + scaled_sprite + padding
    max_baked_w = max(b.width * SCALE for _, _, b in rows)
    label_w = 240
    total_w = pad + SRC_THUMB + pad + label_w + pad + max_baked_w + pad

    out = Image.new("RGB", (total_w, total_h), (40, 40, 40))
    draw = ImageDraw.Draw(out)

    y = pad
    for spec, src, baked in rows:
        # Source thumbnail — scale source to fit SRC_THUMB
        src_img = src.to_image().convert("RGB")
        src_img.thumbnail((SRC_THUMB, SRC_THUMB))
        out.paste(src_img, (pad, y + (row_h - pad - src_img.height) // 2))

        # Label: boss name + info
        label_y = y + 20
        draw.text((pad + SRC_THUMB + pad, label_y), spec.ident.removesuffix("_data"),
                  fill=(255, 255, 255), font=font)
        draw.text((pad + SRC_THUMB + pad, label_y + 20),
                  f"{spec.width}x{spec.height}, pipeline={spec.pipeline}",
                  fill=(200, 200, 200), font=font)
        # Feature count from meta if present
        horns = baked.meta.get("horns", [])
        claws = baked.meta.get("claws", [])
        eyes = baked.meta.get("eyes", [])
        thin = baked.meta.get("thin_features", [])
        info_parts = []
        if horns: info_parts.append(f"horns={len(horns)}")
        if claws: info_parts.append(f"claws={len(claws)}")
        if eyes: info_parts.append(f"eyes={len(eyes)}")
        if thin: info_parts.append(f"thin={len(thin)}")
        if info_parts:
            draw.text((pad + SRC_THUMB + pad, label_y + 40),
                      " ".join(info_parts), fill=(180, 220, 180), font=font)

        # Baked sprite at target × 16
        baked_img = baked.to_upscaled_image(SCALE)
        baked_x = pad + SRC_THUMB + pad + label_w + pad
        baked_y = y + (row_h - pad - baked_img.height) // 2
        out.paste(baked_img, (baked_x, baked_y))

        y += row_h
        draw.line([(pad, y - 1), (total_w - pad, y - 1)], fill=(80, 80, 80))

    out.save(OUT)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()