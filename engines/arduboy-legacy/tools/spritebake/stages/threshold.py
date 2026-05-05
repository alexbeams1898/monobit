"""Threshold stages — turn the float gray image into a binary mask.

Every thresholding technique ends a pipeline's "fluid" phase; the result
is a clean boolean SpriteData. Earlier stages may have attached
`meta['edges']` (Sobel) or `meta['distance']` which these threshold
variants can consume.
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
    """Thresholding strips the gray image from meta to signal we're
    now in binary-land. Preserves other meta entries."""
    new_meta = {k: v for k, v in sprite.meta.items() if k != "gray"}
    return SpriteData(pixels=mask.astype(bool), meta=new_meta)


@register_stage("threshold_fixed")
def threshold_fixed(sprite: SpriteData, *, value: int = 128) -> SpriteData:
    """Hard threshold at a fixed gray level."""
    return _finalize(sprite, _gray(sprite) >= value)


@register_stage("threshold_otsu")
def threshold_otsu(sprite: SpriteData, *, bias: int = 0) -> SpriteData:
    """Otsu's method — maximizes between-class variance.

    `bias` is added to the computed threshold (positive = stricter,
    fewer white pixels).
    """
    gray = _gray(sprite)
    thr = _otsu(gray) + bias
    return _finalize(sprite, gray >= thr)


def _otsu(gray: np.ndarray) -> int:
    hist, _ = np.histogram(gray.clip(0, 255).astype(np.uint8), bins=256, range=(0, 256))
    total = hist.sum()
    if total == 0:
        return 128
    sum_all = (np.arange(256) * hist).sum()
    sum_b = 0.0
    w_b = 0
    var_max = 0.0
    thr = 128
    for t in range(256):
        w_b += int(hist[t])
        if w_b == 0:
            continue
        w_f = total - w_b
        if w_f == 0:
            break
        sum_b += t * hist[t]
        m_b = sum_b / w_b
        m_f = (sum_all - sum_b) / w_f
        v = w_b * w_f * (m_b - m_f) ** 2
        if v > var_max:
            var_max = v
            thr = t
    return thr


@register_stage("threshold_percentile")
def threshold_percentile(sprite: SpriteData, *, white_fraction: float = 0.25) -> SpriteData:
    """Pick the threshold so ~`white_fraction` of pixels end up lit.

    Useful when a target white-density is known (e.g. "sprites should be
    ~30% filled"). Ignores the actual luminance distribution shape.
    """
    gray = _gray(sprite).ravel()
    n = gray.size
    want = int(n * max(0.0, min(1.0, 1.0 - white_fraction)))
    thr = int(np.partition(gray, want)[want]) if 0 <= want < n else 128
    return _finalize(sprite, _gray(sprite) >= thr)


@register_stage("threshold_edge_biased")
def threshold_edge_biased(sprite: SpriteData, *, base: int = 128,
                          edge_weight: float = 0.5,
                          edge_key: str = "edges") -> SpriteData:
    """Per-pixel threshold biased lower where edges are strong.

    Pixels with high Sobel magnitude (meta[edge_key]) become lit at a
    lower gray value, preserving silhouette boundaries at the expense
    of interior texture. Requires a prior `sobel_edges` stage.
    """
    gray = _gray(sprite)
    edges = sprite.meta.get(edge_key)
    if edges is None:
        # Degrade gracefully to fixed threshold.
        return _finalize(sprite, gray >= base)
    # Normalize edges to 0..1 and map to threshold delta
    e = edges / max(1.0, float(edges.max()))
    local_thr = base - (e * (base * edge_weight))
    return _finalize(sprite, gray >= local_thr)


@register_stage("threshold_distance_weighted")
def threshold_distance_weighted(sprite: SpriteData, *, base: int = 128,
                                interior_weight: float = 0.2) -> SpriteData:
    """Tighten threshold as distance-from-nearest-edge grows.

    Edge pixels use `base`; interior pixels use a higher threshold,
    making it harder for interior texture to survive. Resulting
    silhouettes tend to be cleaner. Requires prior Sobel / no edges is OK.
    """
    gray = _gray(sprite)
    # Crude distance transform: iterate a mask dilation from the edges.
    edges = sprite.meta.get("edges")
    if edges is None:
        # Compute a quick approximation
        low = gray < 32
        edge_mask = np.zeros_like(low, dtype=bool)
        edge_mask[:-1, :] |= low[:-1, :] != low[1:, :]
        edge_mask[1:, :]  |= low[:-1, :] != low[1:, :]
        edge_mask[:, :-1] |= low[:, :-1] != low[:, 1:]
        edge_mask[:, 1:]  |= low[:, :-1] != low[:, 1:]
    else:
        edge_mask = edges > (0.3 * edges.max())

    # BFS distance from edges
    dist = np.full(gray.shape, np.inf, dtype=np.float32)
    dist[edge_mask] = 0
    # Simple multi-pass: for small sprites, 8-connected iterative min works fine.
    for _ in range(max(gray.shape)):
        prev = dist.copy()
        # Check all 8 neighbors (shifted + 1)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                shifted = np.roll(prev, (dy, dx), axis=(0, 1))
                # Don't wrap
                if dy == -1: shifted[-1, :] = np.inf
                if dy == 1:  shifted[0, :]  = np.inf
                if dx == -1: shifted[:, -1] = np.inf
                if dx == 1:  shifted[:, 0]  = np.inf
                cost = 1.0 if (dx == 0 or dy == 0) else 1.4
                dist = np.minimum(dist, shifted + cost)
        if (dist == prev).all():
            break

    dist_norm = dist / max(1.0, float(np.where(np.isfinite(dist), dist, 0).max()))
    dist_norm = np.where(np.isfinite(dist_norm), dist_norm, 1.0)
    local_thr = base + (dist_norm * base * interior_weight)
    return _finalize(sprite, gray >= local_thr)
