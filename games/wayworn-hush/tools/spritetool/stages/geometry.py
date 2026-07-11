"""Color-agnostic geometry stages (logic ported from the Arduboy tool, widened
to RGBA): crop to content, resize, pad to a target size."""

from __future__ import annotations

import numpy as np
from PIL import Image

from ..core import SpriteData, register_stage


@register_stage("autocrop")
def autocrop(sprite: SpriteData) -> SpriteData:
    """Crop to the alpha bounding box (drop fully-transparent border)."""
    alpha = sprite.pixels[:, :, 3]
    ys, xs = np.where(alpha > 0)
    if len(ys) == 0:
        return sprite.copy()  # fully transparent -> leave as-is
    y0, y1 = ys.min(), ys.max() + 1
    x0, x1 = xs.min(), xs.max() + 1
    return SpriteData(sprite.pixels[y0:y1, x0:x1].copy(), dict(sprite.meta))


@register_stage("resize")
def resize(sprite: SpriteData, width: int, height: int) -> SpriteData:
    """Nearest-neighbor resize to exactly (width, height). Nearest preserves the
    hard pixel edges (no interpolated fringe)."""
    im = Image.fromarray(sprite.pixels, "RGBA")
    im = im.resize((int(width), int(height)), Image.NEAREST)
    return SpriteData(np.array(im, dtype=np.uint8), dict(sprite.meta))


@register_stage("pad")
def pad(sprite: SpriteData, width: int, height: int) -> SpriteData:
    """Center the sprite on a transparent canvas of (width, height). Content
    larger than the canvas is an error (author to fit)."""
    w, h = int(width), int(height)
    if sprite.width > w or sprite.height > h:
        raise ValueError(
            f"content {sprite.width}x{sprite.height} exceeds pad target {w}x{h}"
        )
    canvas = np.zeros((h, w, 4), dtype=np.uint8)
    ox = (w - sprite.width) // 2
    oy = (h - sprite.height) // 2
    canvas[oy : oy + sprite.height, ox : ox + sprite.width] = sprite.pixels
    return SpriteData(canvas, dict(sprite.meta))
