"""Silhouette-aware stages — "outline the important parts."

These are the higher-level shape-preserving techniques we discussed:
outline + fill, thin-feature detection and reinforcement, and
silhouette extraction. They replace or supplement the threshold+morph
combos on sprites where silhouette trumps interior detail.
"""

from __future__ import annotations

import numpy as np

from ..core import SpriteData
from ..pipeline import register_stage
from .filter import _dilate_once, _erode_once


def _gray(sprite: SpriteData) -> np.ndarray:
    if "gray" in sprite.meta:
        return sprite.meta["gray"].astype(np.float32)
    return sprite.pixels.astype(np.float32) * 255.0


def _finalize(sprite: SpriteData, mask: np.ndarray) -> SpriteData:
    new_meta = {k: v for k, v in sprite.meta.items() if k != "gray"}
    return SpriteData(pixels=mask.astype(bool), meta=new_meta)


@register_stage("outline_and_fill")
def outline_and_fill(sprite: SpriteData, *,
                     silhouette_threshold: int = 96,
                     fill: bool = True) -> SpriteData:
    """Extract a solid silhouette from the gray image, optionally filled.

    Pipeline within the stage:
      1. Strong-threshold gray to mark silhouette candidates (strict).
      2. Dilate once to close 1-pixel fissures in the outline.
      3. Optionally flood-fill the interior.

    Trades interior texture for clean outline — ideal at 12..24 px target.
    """
    gray = _gray(sprite)
    strict = gray >= silhouette_threshold
    # Close small fissures
    closed = _dilate_once(strict)
    closed = _erode_once(closed)
    if not fill:
        return _finalize(sprite, closed)
    # Flood-fill from border: everything NOT reachable from the border is "inside"
    h, w = closed.shape
    outside = np.zeros((h, w), dtype=bool)
    stack = []
    for x in range(w):
        if not closed[0, x]:     stack.append((0, x))
        if not closed[h-1, x]:   stack.append((h-1, x))
    for y in range(h):
        if not closed[y, 0]:     stack.append((y, 0))
        if not closed[y, w-1]:   stack.append((y, w-1))
    while stack:
        y, x = stack.pop()
        if y < 0 or x < 0 or y >= h or x >= w:
            continue
        if outside[y, x] or closed[y, x]:
            continue
        outside[y, x] = True
        stack.extend([(y-1, x), (y+1, x), (y, x-1), (y, x+1)])
    interior = ~outside & ~closed
    filled = closed | interior
    return _finalize(sprite, filled)


@register_stage("detect_thin_features")
def detect_thin_features(sprite: SpriteData, *,
                         max_width_ratio: float = 0.05,
                         min_length_ratio: float = 0.03,
                         band_ratio: float = 0.2,
                         min_prominence: float = 0.02,
                         peak_separation_ratio: float = 0.05,
                         attach_as: str = "thin_features",
                         require_symmetric_pair: bool = False,
                         symmetry_tolerance: float = 0.08) -> SpriteData:
    """Find vertical spike features (horns, crowns, weapons, raised tails)
    as PEAKS in the top-edge profile. Stored in normalized coords.

    Algorithm:
      1. For every column, find the topmost lit pixel y-value (or None).
      2. That forms a "top-edge profile" — a 1-D function y_top(x).
      3. Find local MINIMA of this function (since lower y = higher
         in the image) with prominence >= min_prominence * height.
      4. Each local minimum is a spike/horn tip.

    This handles horns that share a base (connected via the head) because
    we look for peaks in the silhouette's upper boundary, not isolated
    feature bands.

    Tunable parameters:
      - min_prominence: how much a peak must rise above its surroundings
      - peak_separation_ratio: min horizontal distance between peaks
      - band_ratio: peaks with y_top > band_ratio * h are ignored
    """
    gray = _gray(sprite)
    h, w = gray.shape
    lit = gray >= 96
    band = max(1, int(h * band_ratio))

    # Build the top-edge profile: for each column, the y of the topmost
    # lit pixel, or `h` (below image) if the column is dark.
    top_profile = np.full(w, h, dtype=np.int32)
    for x in range(w):
        true_pos = np.where(lit[:, x])[0]
        if len(true_pos) > 0:
            top_profile[x] = int(true_pos[0])

    # Smooth the profile slightly so single-pixel noise doesn't create
    # spurious peaks. Simple box average with width ~2% of image width.
    smooth_radius = max(1, int(w * 0.01))
    kernel = np.ones(2 * smooth_radius + 1, dtype=np.float32) / (2 * smooth_radius + 1)
    profile = np.convolve(top_profile.astype(np.float32), kernel, mode="same")

    # Find local minima (peaks in the silhouette — points that are HIGHER
    # in the image than their surroundings, which means LOWER y).
    min_prom_px = max(1.0, min_prominence * h)
    min_sep_px = max(1, int(peak_separation_ratio * w))

    # Naive peak detection — for each candidate index, check it's lower
    # than both 3%-window-left-minimum and 3%-window-right-minimum.
    window = max(5, int(w * 0.03))
    candidates: list[tuple[int, float]] = []  # (x, prominence)
    for x in range(window, w - window):
        local = profile[x - window:x + window + 1]
        if profile[x] != local.min():
            continue
        if profile[x] >= band:
            continue
        # Walk left/right from x until profile rises above profile[x] + min_prom_px
        target = profile[x] + min_prom_px
        left_ok = False
        for lx in range(x - 1, -1, -1):
            if profile[lx] >= target:
                left_ok = True
                break
        right_ok = False
        for rx in range(x + 1, w):
            if profile[rx] >= target:
                right_ok = True
                break
        if left_ok and right_ok:
            # Prominence is approximately how much higher the peak is than its neighboring troughs
            candidates.append((x, float(target - profile[x])))

    # Non-maximum-suppression: drop candidates within min_sep_px of a
    # stronger one.
    candidates.sort(key=lambda c: c[1], reverse=True)
    kept: list[int] = []
    for x, prom in candidates:
        if all(abs(x - kx) >= min_sep_px for kx in kept):
            kept.append(x)

    # Symmetric-pair filter — horns come in pairs straddling the figure's
    # centerline. Demand each peak have a mirror-image partner: for a peak
    # at x, look for another peak near (w-1-x) within tolerance.
    # Drops singleton false-positives (Charon's scythe, Phlegyas's flame,
    # a head apex between two horns).
    if require_symmetric_pair and kept:
        tol_px = max(1, int(w * symmetry_tolerance))
        paired: list[int] = []
        for x in kept:
            mirror_x = (w - 1) - x
            for other in kept:
                if other == x:
                    continue
                if abs(other - mirror_x) <= tol_px:
                    paired.append(x)
                    break
        kept = paired

    min_feature_length_px = max(1, int(h * min_length_ratio))

    features: list[tuple[float, float, float]] = []
    for x in sorted(kept):
        y_top = int(profile[x])
        # Measure run length down from y_top in column x
        run = 0
        y = y_top
        while y < h and lit[y, x]:
            run += 1
            y += 1
        if run < min_feature_length_px:
            # Still record but the reinforcer can use min_target_length instead.
            run = min_feature_length_px
        x_center = x / max(1, w - 1)
        y_top_norm = y_top / max(1, h - 1)
        length_norm = run / max(1, h - 1)
        features.append((x_center, y_top_norm, length_norm))

    new_meta = dict(sprite.meta)
    new_meta[attach_as] = features
    return SpriteData(pixels=sprite.pixels.copy(), meta=new_meta)


@register_stage("reinforce_thin_features")
def reinforce_thin_features(sprite: SpriteData, *,
                            feature_key: str = "thin_features",
                            extend_pixels: int = 1,
                            min_target_width: int = 1,
                            min_target_length: int = 1,
                            max_target_length: int | None = None) -> SpriteData:
    """Re-light the thin features discovered earlier, mapping normalized
    source coords into target-resolution pixels.

    Each feature triple (x_center_norm, y_top_norm, length_norm) from
    `detect_thin_features` is scaled to the current target sprite and
    drawn as a vertical line of at least `min_target_width` columns wide.

    This is the stage that answers "the source has two horns at 10%
    width each; force those pixels on in the target regardless of what
    the threshold decided."
    """
    features = sprite.meta.get(feature_key, [])
    if not features:
        return sprite
    px = sprite.pixels.copy()
    h, w = px.shape
    for feat in features:
        # Accept either normalized tuples (new) or pixel tuples (old).
        if len(feat) != 3:
            continue
        x_c, y_t, length = feat
        if 0.0 <= x_c <= 1.0 and 0.0 <= y_t <= 1.0:
            # Normalized — scale to target
            x_target = int(round(x_c * (w - 1)))
            y_start_target = int(round(y_t * (h - 1)))
            length_target = max(min_target_length, int(round(length * (h - 1))))
        else:
            # Pixel coords in target space (legacy)
            x_target = int(x_c)
            y_start_target = int(y_t)
            length_target = max(min_target_length, int(length))
        if max_target_length is not None:
            length_target = min(length_target, max_target_length)
        y_end = min(h, y_start_target + length_target + extend_pixels)
        y_start = max(0, y_start_target)
        for dx in range(-(min_target_width // 2), min_target_width - (min_target_width // 2)):
            col = x_target + dx
            if 0 <= col < w:
                px[y_start:y_end, col] = True
    return sprite.with_pixels(px)


@register_stage("detect_bottom_features")
def detect_bottom_features(sprite: SpriteData, *,
                           max_width_ratio: float = 0.15,
                           min_length_ratio: float = 0.02,
                           band_ratio: float = 0.1,
                           min_prominence: float = 0.02,
                           peak_separation_ratio: float = 0.05,
                           attach_as: str = "bottom_features") -> SpriteData:
    """Find downward-pointing spike features (claws, fangs, raised spines)
    as PEAKS in the BOTTOM-edge profile. Stored in normalized coords.

    Mirror of detect_thin_features but operates on the bottom of the
    silhouette. The y_bottom profile is the y of the LAST lit pixel in
    each column; local MAXIMA of that profile = lowest points in the
    image = claws/fangs/downward spikes.

    Peaks must lie in the bottom `band_ratio` fraction of the image.
    """
    gray = _gray(sprite)
    h, w = gray.shape
    lit = gray >= 96
    band_start = int(h * (1.0 - band_ratio))

    bottom_profile = np.full(w, -1, dtype=np.int32)
    for x in range(w):
        true_pos = np.where(lit[:, x])[0]
        if len(true_pos) > 0:
            bottom_profile[x] = int(true_pos[-1])

    # Smooth
    smooth_radius = max(1, int(w * 0.01))
    kernel = np.ones(2 * smooth_radius + 1, dtype=np.float32) / (2 * smooth_radius + 1)
    profile = np.convolve(bottom_profile.astype(np.float32), kernel, mode="same")

    min_prom_px = max(1.0, min_prominence * h)
    min_sep_px = max(1, int(peak_separation_ratio * w))
    window = max(5, int(w * 0.03))

    candidates: list[tuple[int, float]] = []
    for x in range(window, w - window):
        local = profile[x - window:x + window + 1]
        if profile[x] != local.max():
            continue
        if profile[x] < band_start:
            continue
        target = profile[x] - min_prom_px
        left_ok = False
        for lx in range(x - 1, -1, -1):
            if profile[lx] <= target:
                left_ok = True
                break
        right_ok = False
        for rx in range(x + 1, w):
            if profile[rx] <= target:
                right_ok = True
                break
        if left_ok and right_ok:
            candidates.append((x, float(profile[x] - target)))

    candidates.sort(key=lambda c: c[1], reverse=True)
    kept: list[int] = []
    for x, prom in candidates:
        if all(abs(x - kx) >= min_sep_px for kx in kept):
            kept.append(x)

    min_feature_length_px = max(1, int(h * min_length_ratio))

    features: list[tuple[float, float, float]] = []
    for x in sorted(kept):
        y_bottom = int(profile[x])
        # Walk UP from y_bottom until we exit the column's lit run
        y = y_bottom
        run = 0
        while y >= 0 and lit[y, x]:
            run += 1
            y -= 1
        if run < min_feature_length_px:
            run = min_feature_length_px
        y_start = max(0, y_bottom - run + 1)
        x_center = x / max(1, w - 1)
        y_top_norm = y_start / max(1, h - 1)
        length_norm = run / max(1, h - 1)
        features.append((x_center, y_top_norm, length_norm))

    new_meta = dict(sprite.meta)
    new_meta[attach_as] = features
    return SpriteData(pixels=sprite.pixels.copy(), meta=new_meta)


@register_stage("detect_dark_spots")
def detect_dark_spots(sprite: SpriteData, *,
                     min_size_ratio: float = 0.0005,
                     max_size_ratio: float = 0.005,
                     max_brightness: int = 60,
                     require_surrounded: bool = True,
                     y_min_ratio: float = 0.0,
                     y_max_ratio: float = 1.0,
                     attach_as: str = "dark_spots") -> SpriteData:
    """Find small dark connected regions inside a bright silhouette —
    e.g. eyes, nostrils, mouth. Stored as normalized centroids.

    Algorithm:
      1. Build a "very dark" mask from gray <= max_brightness.
      2. Build a "very bright" mask from gray >= 128.
      3. For each connected component of the dark mask, check:
         - size is within [min_size_ratio, max_size_ratio] * image area
         - if require_surrounded, its bounding box is mostly surrounded
           by bright pixels (at least 60% of pixels in a 1.5x-bbox neighborhood are bright).
      4. Record normalized centroid (x_c, y_c, radius) triples.

    `reinforce_dark_spots` (next) extinguishes matching pixels in the
    target binary so the eye stays a black hole in the face.
    """
    gray = _gray(sprite)
    h, w = gray.shape
    dark = gray <= max_brightness
    bright = gray >= 128

    img_area = h * w
    min_size = max(1, int(img_area * min_size_ratio))
    max_size = max(min_size + 1, int(img_area * max_size_ratio))

    visited = np.zeros_like(dark)
    spots: list[tuple[float, float, float]] = []  # (x_c_norm, y_c_norm, radius_norm)

    offsets = [(-1, 0), (1, 0), (0, -1), (0, 1)]
    y_min = int(h * y_min_ratio)
    y_max = int(h * y_max_ratio)

    for y in range(y_min, y_max):
        for x in range(w):
            if not dark[y, x] or visited[y, x]:
                continue
            # BFS
            stack = [(y, x)]
            component = []
            while stack:
                cy, cx = stack.pop()
                if cy < 0 or cx < 0 or cy >= h or cx >= w:
                    continue
                if visited[cy, cx] or not dark[cy, cx]:
                    continue
                visited[cy, cx] = True
                component.append((cy, cx))
                for dy, dx in offsets:
                    stack.append((cy + dy, cx + dx))
            if not (min_size <= len(component) <= max_size):
                continue
            ys = [p[0] for p in component]
            xs = [p[1] for p in component]
            y_min, y_max = min(ys), max(ys)
            x_min, x_max = min(xs), max(xs)
            if require_surrounded:
                # Sample ring just outside the bbox
                pad = max(1, (x_max - x_min + y_max - y_min) // 4)
                y_lo, y_hi = max(0, y_min - pad), min(h, y_max + pad + 1)
                x_lo, x_hi = max(0, x_min - pad), min(w, x_max + pad + 1)
                ring_mask = np.zeros((y_hi - y_lo, x_hi - x_lo), dtype=bool)
                ring_mask[:, :] = True
                # Exclude the dark component from the ring
                for cy, cx in component:
                    ring_mask[cy - y_lo, cx - x_lo] = False
                ring_pixels_total = int(ring_mask.sum())
                if ring_pixels_total == 0:
                    continue
                ring_bright = int((bright[y_lo:y_hi, x_lo:x_hi] & ring_mask).sum())
                if ring_bright / ring_pixels_total < 0.6:
                    continue
            cy_c = sum(ys) / len(ys)
            cx_c = sum(xs) / len(xs)
            radius = max((y_max - y_min), (x_max - x_min)) / 2.0
            spots.append((cx_c / max(1, w - 1),
                          cy_c / max(1, h - 1),
                          radius / max(1, max(h, w) - 1)))

    new_meta = dict(sprite.meta)
    new_meta[attach_as] = spots
    return SpriteData(pixels=sprite.pixels.copy(), meta=new_meta)


@register_stage("reinforce_dark_spots")
def reinforce_dark_spots(sprite: SpriteData, *,
                         feature_key: str = "dark_spots",
                         min_target_radius: int = 0) -> SpriteData:
    """Extinguish target pixels at detected dark-spot centers.

    For each (x_c, y_c, radius) in meta[feature_key] (normalized), clear
    (set to False) the pixel at that target position, plus any pixel
    within `max(min_target_radius, radius*target_dim)` of it.

    Use case: eyes, nostrils, mouth. After a threshold/dither fills the
    face solid, this re-opens the dark holes where the source had them.
    """
    spots = sprite.meta.get(feature_key, [])
    if not spots:
        return sprite
    px = sprite.pixels.copy()
    h, w = px.shape
    for (x_c, y_c, r) in spots:
        xi = int(round(x_c * (w - 1)))
        yi = int(round(y_c * (h - 1)))
        radius = max(min_target_radius, int(round(r * max(h, w))))
        if radius <= 0:
            if 0 <= yi < h and 0 <= xi < w:
                px[yi, xi] = False
        else:
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    if dy * dy + dx * dx > radius * radius:
                        continue
                    ny, nx = yi + dy, xi + dx
                    if 0 <= ny < h and 0 <= nx < w:
                        px[ny, nx] = False
    return sprite.with_pixels(px)


@register_stage("silhouette_preserve")
def silhouette_preserve(sprite: SpriteData, *,
                        source_edge_threshold: int = 80,
                        protect_radius: int = 1) -> SpriteData:
    """After a threshold/dither, protect a band around the silhouette
    boundary from being lost.

    Works by dilating the current binary mask by `protect_radius` and
    AND-ing with a strict silhouette mask derived from the gray. Pixels
    near the existing binary that are also strongly-lit in gray survive.

    Best used AFTER a threshold and before a morph_open to guarantee
    the outline doesn't get eroded.
    """
    gray = _gray(sprite) if "gray" in sprite.meta else (sprite.pixels.astype(np.float32) * 255.0)
    strict = gray >= source_edge_threshold
    band = sprite.pixels.copy()
    for _ in range(protect_radius):
        band = _dilate_once(band)
    protected = strict & band
    return sprite.with_pixels(sprite.pixels | protected)


@register_stage("spirit_fade_bottom")
def spirit_fade_bottom(sprite: SpriteData, *,
                       fade_ratio: float = 0.30,
                       top_keep: float = 0.90,
                       bottom_keep: float = 0.15) -> SpriteData:
    """Dissolve the bottom band of the sprite so the figure looks like a
    spirit surfacing from pitch rather than an amputated body.

    Over the bottom `fade_ratio` of rows, lit pixels survive with
    probability that ramps linearly from `top_keep` (at the top of the
    fade band) to `bottom_keep` (at the last row). Uses a deterministic
    4x4 Bayer threshold matrix so every bake of the same sprite produces
    the same erosion — reproducible, not random.

    Top rows (above the fade band) are untouched.
    """
    px = sprite.pixels.copy()
    h, w = px.shape
    band_h = max(1, int(h * fade_ratio))
    band_start = h - band_h
    # 4x4 Bayer threshold matrix — values in [0, 1). Pixel survives when
    # keep_prob > bayer[y%4, x%4], giving a regular dithered erosion
    # pattern that looks organic but is deterministic.
    bayer = np.array([
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
    ], dtype=np.float32) / 16.0
    for y in range(band_start, h):
        # Linear ramp: t=0 at band_start, t=1 at last row.
        t = (y - band_start) / max(1, band_h - 1) if band_h > 1 else 1.0
        keep_prob = top_keep + (bottom_keep - top_keep) * t
        for x in range(w):
            if px[y, x] and keep_prob <= bayer[y % 4, x % 4]:
                px[y, x] = False
    return sprite.with_pixels(px)


@register_stage("stash_silhouette_from_gray")
def stash_silhouette_from_gray(sprite: SpriteData, *,
                               threshold: int = 96,
                               close_iterations: int = 1) -> SpriteData:
    """Compute a hard silhouette mask from meta['gray'] and stash it in
    meta['silhouette'] for a later masking stage to consume.

    Must run AFTER downscale (so the stashed mask matches the target
    resolution) and BEFORE any stage that discards meta['gray']
    (e.g. bayer_ordered_dither). close_iterations applies dilate-then-erode
    to seal 1-px fissures without bloating the silhouette.
    """
    gray = _gray(sprite)
    mask = gray >= threshold
    for _ in range(close_iterations):
        mask = _erode_once(_dilate_once(mask))
    new_meta = dict(sprite.meta)
    new_meta["silhouette"] = mask.astype(bool)
    return SpriteData(pixels=sprite.pixels.copy(), meta=new_meta)


@register_stage("apply_stashed_silhouette")
def apply_stashed_silhouette(sprite: SpriteData) -> SpriteData:
    """AND sprite.pixels with meta['silhouette']. No-op if no mask stashed.

    Pair with stash_silhouette_from_gray around a dither stage to keep the
    tonal dither inside the figure while killing the background dot-grid
    that Bayer otherwise paints across empty pitch.
    """
    sil = sprite.meta.get("silhouette")
    if sil is None or sil.shape != sprite.pixels.shape:
        return sprite
    return sprite.with_pixels(sprite.pixels & sil)
