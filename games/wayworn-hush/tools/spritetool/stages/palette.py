"""Palette stages: the GBA hardware discipline.

rgb555_snap    -- snap RGB to the GBA 15-bit color grid (32 levels/channel).
palette_reduce -- reduce to <=16 colors (15 opaque + transparent), GBA 4bpp.

The 'Emerald feel' comes from the artist's color choices; these stages enforce
only the two hardware truths (valid GBA colors + the 16-color cap).
"""

from __future__ import annotations

import sys

import numpy as np

from ..core import SpriteData, register_stage

# GBA sprite palette: 16 entries, one reserved for transparency -> 15 opaque.
MAX_OPAQUE_COLORS = 15


def _opaque_colors(px: np.ndarray) -> np.ndarray:
    opaque = px[:, :, 3] == 255
    return np.unique(px[:, :, :3][opaque], axis=0)


def _snap_channel(c: np.ndarray) -> np.ndarray:
    # 0..255 -> nearest of 32 levels -> back to 0..255. round(c/255*31)/31*255.
    levels = np.rint(c.astype(np.float32) / 255.0 * 31.0)
    return np.rint(levels / 31.0 * 255.0).astype(np.uint8)


@register_stage("rgb555_snap")
def rgb555_snap(sprite: SpriteData) -> SpriteData:
    out = sprite.copy()
    px = out.pixels
    # Snap RGB; force alpha to 1-bit (GBA sprites have no partial transparency).
    px[:, :, 0] = _snap_channel(px[:, :, 0])
    px[:, :, 1] = _snap_channel(px[:, :, 1])
    px[:, :, 2] = _snap_channel(px[:, :, 2])
    px[:, :, 3] = np.where(px[:, :, 3] >= 128, 255, 0).astype(np.uint8)
    return out


def _median_cut(colors: np.ndarray, n: int) -> np.ndarray:
    """Reduce an (M,3) array of RGB colors to n representative colors via
    median cut. Returns an (n',3) palette (n' <= n)."""
    boxes = [colors]
    while len(boxes) < n:
        # Pick the box with the largest channel spread (by index; numpy arrays
        # can't be compared by equality for list.remove).
        best_i = -1
        best_spread = -1.0
        for i, b in enumerate(boxes):
            if len(b) <= 1:
                continue
            spread = float((b.max(axis=0) - b.min(axis=0)).max())
            if spread > best_spread:
                best_spread, best_i = spread, i
        if best_i < 0:
            break  # nothing left splittable
        box = boxes.pop(best_i)
        axis = int((box.max(axis=0) - box.min(axis=0)).argmax())
        order = box[box[:, axis].argsort()]
        mid = len(order) // 2
        boxes.append(order[:mid])
        boxes.append(order[mid:])
    return np.array([b.mean(axis=0) for b in boxes], dtype=np.float32)


@register_stage("palette_check")
def palette_check(sprite: SpriteData, max_colors: int = 16) -> SpriteData:
    """DEFAULT palette stage: snap to RGB555 and count colors, but do NOT alter
    the artist's color choices. If the sprite exceeds the opaque-color cap, warn
    (the artist should hand-reduce in Aseprite -- artful beats auto-clustering).
    Within budget, colors pass through exactly."""
    out = rgb555_snap(sprite)
    distinct = _opaque_colors(out.pixels)
    out.meta["palette"] = [tuple(int(v) for v in c) for c in distinct]
    ident = sprite.meta.get("ident", "sprite")
    cap = min(MAX_OPAQUE_COLORS, max_colors - 1)
    if len(distinct) > cap:
        print(
            f"[spritetool] WARN {ident}: {len(distinct)} opaque colors exceeds "
            f"GBA cap of {cap} -- reduce the palette in Aseprite (or opt into "
            f"palette_reduce). Colors left unchanged.",
            file=sys.stderr,
        )
    return out


@register_stage("palette_reduce")
def palette_reduce(sprite: SpriteData, max_colors: int = 16) -> SpriteData:
    """OPT-IN auto-reduction: cluster to <=cap colors. Use only when you want the
    machine to reduce for you; the default (palette_check) preserves your colors."""
    out = rgb555_snap(sprite)
    px = out.pixels
    opaque = px[:, :, 3] == 255
    max_opaque = min(MAX_OPAQUE_COLORS, max_colors - 1)
    rgb = px[:, :, :3]
    opaque_rgb = rgb[opaque]
    if len(opaque_rgb) == 0:
        out.meta["palette"] = []
        return out

    distinct = np.unique(opaque_rgb, axis=0)
    if len(distinct) <= max_opaque:
        out.meta["palette"] = [tuple(int(v) for v in c) for c in distinct]
        return out

    palette = _median_cut(distinct.astype(np.float32), max_opaque)
    flat = opaque_rgb.astype(np.float32)
    d = ((flat[:, None, :] - palette[None, :, :]) ** 2).sum(axis=2)
    nearest = d.argmin(axis=1)
    remapped = np.rint(palette[nearest]).astype(np.uint8)
    rgb[opaque] = remapped

    used = np.unique(remapped, axis=0)
    out.meta["palette"] = [tuple(int(v) for v in c) for c in used]
    return out
