"""Morphological post-processing — run AFTER thresholding to denoise
silhouettes without destroying thin features.

Stages:
  - morph_open_speckle: kill white pixels with fewer than N white
    8-neighbors. Aggressive clean, but eats horn tips if not tuned.
  - morph_close_holes: fill isolated black pixels (6+ white neighbors).
  - vert_feature_boost: preserve vertical spike-tips by extending
    qualifying columns upward by 1 pixel.
  - connected_component_filter: remove blobs below a size threshold
    (kill specks without touching anything connected to the main body).
  - majority_smooth: each pixel becomes the majority of its 3x3 neighborhood.
"""

from __future__ import annotations

import numpy as np

from ..core import SpriteData
from ..pipeline import register_stage


@register_stage("morph_open_speckle")
def morph_open_speckle(sprite: SpriteData, *, min_neighbors: int = 2) -> SpriteData:
    """Remove white pixels with < `min_neighbors` white 8-neighbors.

    min_neighbors=1 kills only pure singletons (preserves narrow spikes).
    min_neighbors=2 is aggressive — watch for horn-tip loss.
    """
    px = sprite.pixels
    h, w = px.shape
    out = np.zeros_like(px)
    padded = np.pad(px, 1, mode="constant", constant_values=False)
    neighbors = (
        padded[:-2, :-2].astype(np.int8) + padded[:-2, 1:-1] + padded[:-2, 2:]
        + padded[1:-1, :-2]                                 + padded[1:-1, 2:]
        + padded[2:,  :-2]                + padded[2:, 1:-1] + padded[2:, 2:]
    )
    out[:] = px & (neighbors >= min_neighbors)
    return sprite.with_pixels(out)


@register_stage("morph_close_holes")
def morph_close_holes(sprite: SpriteData, *, min_neighbors: int = 6) -> SpriteData:
    """Fill black pixels surrounded by `min_neighbors` or more white
    8-neighbors. min_neighbors=6 fills 1-2 pixel holes cleanly; =4 is
    aggressive (can distort texture); =8 is strict."""
    px = sprite.pixels
    padded = np.pad(px, 1, mode="constant", constant_values=False)
    neighbors = (
        padded[:-2, :-2].astype(np.int8) + padded[:-2, 1:-1] + padded[:-2, 2:]
        + padded[1:-1, :-2]                                 + padded[1:-1, 2:]
        + padded[2:,  :-2]                + padded[2:, 1:-1] + padded[2:, 2:]
    )
    fill = (~px) & (neighbors >= min_neighbors)
    return sprite.with_pixels(px | fill)


@register_stage("vert_feature_boost")
def vert_feature_boost(sprite: SpriteData, *,
                       band_ratio: float = 1/3, min_run: int = 2) -> SpriteData:
    """Extend columns with a qualifying run upward by one pixel.

    For each column, find the topmost True pixel. If it's in the top
    `band_ratio` of the image AND has a continuous run of `min_run`+
    True pixels starting there, light the pixel directly above it.
    Preserves narrow vertical features (horns, spikes, tails-up).

    Does nothing if the topmost pixel is already at y=0.
    """
    px = sprite.pixels.copy()
    h, w = px.shape
    band = int(h * band_ratio)
    for x in range(w):
        # Find topmost True
        col = px[:, x]
        true_rows = np.where(col)[0]
        if len(true_rows) == 0:
            continue
        top = int(true_rows[0])
        if top == 0 or top >= band:
            continue
        # Count run length
        run = 0
        y = top
        while y < h and col[y]:
            run += 1
            y += 1
            if run >= min_run:
                break
        if run >= min_run:
            px[top - 1, x] = True
    return sprite.with_pixels(px)


@register_stage("connected_component_filter")
def connected_component_filter(sprite: SpriteData, *, min_size: int = 3,
                               connectivity: int = 8) -> SpriteData:
    """Remove connected components smaller than `min_size` pixels.

    connectivity: 4 or 8. Labels every white blob via flood-fill and
    zeros out any blob below the size threshold. Typically leaves the
    main body intact and only eats noise.
    """
    px = sprite.pixels.copy()
    h, w = px.shape
    visited = np.zeros_like(px)
    out = np.zeros_like(px)

    if connectivity == 8:
        offsets = [(dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                   if not (dy == 0 and dx == 0)]
    else:
        offsets = [(-1, 0), (1, 0), (0, -1), (0, 1)]

    for y in range(h):
        for x in range(w):
            if not px[y, x] or visited[y, x]:
                continue
            # BFS
            stack = [(y, x)]
            component = []
            while stack:
                cy, cx = stack.pop()
                if cy < 0 or cx < 0 or cy >= h or cx >= w:
                    continue
                if visited[cy, cx] or not px[cy, cx]:
                    continue
                visited[cy, cx] = True
                component.append((cy, cx))
                for dy, dx in offsets:
                    stack.append((cy + dy, cx + dx))
            if len(component) >= min_size:
                for cy, cx in component:
                    out[cy, cx] = True
    return sprite.with_pixels(out)


@register_stage("majority_smooth")
def majority_smooth(sprite: SpriteData, *, iterations: int = 1,
                    threshold: int = 5) -> SpriteData:
    """Each pixel becomes True if `threshold`+ of its 3x3 neighborhood
    (including itself, 9 cells total) are True. Smooths jagged edges.

    threshold=5 is majority (strict). threshold=4 biases toward filling.
    """
    px = sprite.pixels.copy()
    for _ in range(iterations):
        padded = np.pad(px, 1, mode="constant", constant_values=False)
        s = np.zeros_like(px, dtype=np.int8)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                s = s + padded[1 + dy:1 + dy + px.shape[0],
                               1 + dx:1 + dx + px.shape[1]].astype(np.int8)
        px = s >= threshold
    return sprite.with_pixels(px)
