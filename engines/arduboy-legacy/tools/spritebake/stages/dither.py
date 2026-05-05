"""Dithering stages — approximate tone with scattered pixels.

Good for preserving source-art texture at sizes where outline detection
would flatten everything. Typically ends a pipeline (replaces
threshold_*). Two variants:

  - floyd_steinberg: classic full-image error diffusion. Produces noise,
    great for preserving tonal range.
  - edge_preserving_dither: dithers interior pixels only, leaving edge
    pixels solid. Best of both worlds for bosses with interior shading.
"""

from __future__ import annotations

import numpy as np

from ..core import SpriteData
from ..pipeline import register_stage


def _gray(sprite: SpriteData) -> np.ndarray:
    if "gray" in sprite.meta:
        return sprite.meta["gray"].astype(np.float32)
    return sprite.pixels.astype(np.float32) * 255.0


def _finalize(sprite: SpriteData, mask: np.ndarray) -> SpriteData:
    new_meta = {k: v for k, v in sprite.meta.items() if k != "gray"}
    return SpriteData(pixels=mask.astype(bool), meta=new_meta)


@register_stage("floyd_steinberg")
def floyd_steinberg(sprite: SpriteData, *, threshold: int = 128) -> SpriteData:
    """Standard Floyd-Steinberg 1-bit error diffusion."""
    gray = _gray(sprite).copy()
    h, w = gray.shape
    mask = np.zeros((h, w), dtype=bool)
    for y in range(h):
        for x in range(w):
            old = gray[y, x]
            new = 255.0 if old >= threshold else 0.0
            mask[y, x] = new > 0
            err = old - new
            if x + 1 < w:
                gray[y, x + 1] += err * 7 / 16
            if y + 1 < h:
                if x > 0:
                    gray[y + 1, x - 1] += err * 3 / 16
                gray[y + 1, x]         += err * 5 / 16
                if x + 1 < w:
                    gray[y + 1, x + 1] += err * 1 / 16
    return _finalize(sprite, mask)


@register_stage("edge_preserving_dither")
def edge_preserving_dither(sprite: SpriteData, *,
                           edge_threshold: int = 80,
                           interior_threshold: int = 128) -> SpriteData:
    """Floyd-Steinberg dither, but edge pixels are solid-lit if strong.

    Uses meta['edges'] if available, otherwise computes a quick Sobel.
    Edge pixels with magnitude > `edge_threshold` are forced ON.
    Interior pixels go through Floyd-Steinberg with `interior_threshold`.
    """
    gray = _gray(sprite).copy()
    edges = sprite.meta.get("edges")
    if edges is None:
        # Quick Sobel on the fly
        gx = np.zeros_like(gray)
        gy = np.zeros_like(gray)
        gx[:, 1:-1] = gray[:, 2:] - gray[:, :-2]
        gy[1:-1, :] = gray[2:, :] - gray[:-2, :]
        edges = np.sqrt(gx * gx + gy * gy)
        if edges.max() > 0:
            edges = edges * (255.0 / edges.max())

    h, w = gray.shape
    mask = np.zeros((h, w), dtype=bool)
    edge_mask = edges >= edge_threshold
    for y in range(h):
        for x in range(w):
            if edge_mask[y, x] and gray[y, x] >= 32:
                mask[y, x] = True
                continue
            old = gray[y, x]
            new = 255.0 if old >= interior_threshold else 0.0
            mask[y, x] = new > 0
            err = old - new
            if x + 1 < w:
                gray[y, x + 1] += err * 7 / 16
            if y + 1 < h:
                if x > 0:
                    gray[y + 1, x - 1] += err * 3 / 16
                gray[y + 1, x]         += err * 5 / 16
                if x + 1 < w:
                    gray[y + 1, x + 1] += err * 1 / 16
    return _finalize(sprite, mask)


@register_stage("bayer_ordered_dither")
def bayer_ordered_dither(sprite: SpriteData, *, matrix: int = 4,
                         threshold: int = 128) -> SpriteData:
    """Ordered (Bayer) dither. Less natural than Floyd-Steinberg but
    patterned consistently — good for static backgrounds or tiled
    textures. matrix: 2, 4, or 8 (matrix size NxN)."""
    gray = _gray(sprite)
    if matrix == 2:
        m = np.array([[0, 2], [3, 1]]) / 4.0 * 255
    elif matrix == 8:
        m = np.array([
            [ 0, 32,  8, 40,  2, 34, 10, 42],
            [48, 16, 56, 24, 50, 18, 58, 26],
            [12, 44,  4, 36, 14, 46,  6, 38],
            [60, 28, 52, 20, 62, 30, 54, 22],
            [ 3, 35, 11, 43,  1, 33,  9, 41],
            [51, 19, 59, 27, 49, 17, 57, 25],
            [15, 47,  7, 39, 13, 45,  5, 37],
            [63, 31, 55, 23, 61, 29, 53, 21],
        ]) / 64.0 * 255
    else:  # 4
        m = np.array([
            [ 0,  8,  2, 10],
            [12,  4, 14,  6],
            [ 3, 11,  1,  9],
            [15,  7, 13,  5],
        ]) / 16.0 * 255

    h, w = gray.shape
    mh, mw = m.shape
    tiled = np.tile(m, ((h + mh - 1) // mh, (w + mw - 1) // mw))[:h, :w]
    mask = gray >= tiled
    return _finalize(sprite, mask)
