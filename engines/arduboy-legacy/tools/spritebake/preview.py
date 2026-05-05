"""Preview renderer — side-by-side comparison of sprites and pipelines.

Designed for tuning pipelines against a set of sources. Outputs a single
PNG with a grid of upscaled 1-bit sprites. Supports:
  - Per-sprite columns (compare sprites across pipelines)
  - Per-pipeline rows (see every pipeline variant side-by-side)
  - Labels on rows and columns
"""

from __future__ import annotations

import numpy as np
from pathlib import Path
from typing import Iterable

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:  # pragma: no cover
    raise ImportError("spritebake preview requires Pillow") from e

from .core import SpriteData


def render_matrix(
    rows: list[tuple[str, list[SpriteData]]],
    col_names: list[str] | None = None,
    *,
    scale: int = 6,
    pad: int = 6,
    label_width: int = 260,
    bg: tuple[int, int, int] = (50, 50, 50),
    fg_text: tuple[int, int, int] = (230, 230, 230),
) -> Image.Image:
    """Render a row-per-pipeline × column-per-sprite matrix.

    Args:
      rows: [(row_label, [sprite_for_col_0, sprite_for_col_1, ...]), ...]
      col_names: optional labels for each column (list matches the sprite
        list length)
    """
    if not rows:
        raise ValueError("no rows to render")

    n_cols = max(len(sprites) for _, sprites in rows)
    if col_names and len(col_names) != n_cols:
        raise ValueError(f"col_names length {len(col_names)} != column count {n_cols}")

    # Determine cell sizes per column (each column's width = max sprite width × scale)
    col_w: list[int] = []
    col_h: list[int] = []
    for j in range(n_cols):
        mw = 1
        mh = 1
        for _, sprites in rows:
            if j < len(sprites):
                s = sprites[j]
                if s.width > mw: mw = s.width
                if s.height > mh: mh = s.height
        col_w.append(mw * scale)
        col_h.append(mh * scale)

    row_h = max(col_h) + pad * 2
    total_w = label_width + sum(col_w) + pad * (n_cols + 1)
    header_h = 22 if col_names else 0
    total_h = header_h + row_h * len(rows) + pad

    out = Image.new("RGB", (total_w, total_h), bg)
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.truetype("arial.ttf", 12)
        font_small = ImageFont.truetype("arial.ttf", 10)
    except OSError:
        font = ImageFont.load_default()
        font_small = font

    if col_names:
        x = label_width + pad
        for j, nm in enumerate(col_names):
            draw.text((x, 4), nm, fill=fg_text, font=font_small)
            x += col_w[j] + pad

    y = header_h
    for label, sprites in rows:
        draw.text((8, y + row_h // 2 - 7), label, fill=fg_text, font=font)
        x = label_width + pad
        for j in range(n_cols):
            if j < len(sprites):
                sprite = sprites[j]
                up = sprite.to_upscaled_image(scale)
                out.paste(up, (x + (col_w[j] - up.width) // 2,
                               y + (row_h - up.height) // 2))
            x += col_w[j] + pad
        y += row_h
        draw.line([(pad, y - 1), (total_w - pad, y - 1)], fill=(80, 80, 80))

    return out


def render_single(sprite: SpriteData, *, scale: int = 8) -> Image.Image:
    """Render a single sprite at the given scale."""
    return sprite.to_upscaled_image(scale)
