"""Cropping, padding, and aspect-fitting stages.

Operate on either a binary SpriteData (as at the end of a pipeline) or a
grayscale-encoded SpriteData where meta['gray'] holds the float array
(for pre-threshold shaping).

Convention across stages: a stage may return a SpriteData whose .pixels
is a boolean mask AND populate meta['gray'] with a float gray image at
matching resolution, so later stages can still see the grayscale.
"""

from __future__ import annotations

import numpy as np

from ..core import SpriteData
from ..pipeline import register_stage


def _gray_or_pixels(sprite: SpriteData) -> np.ndarray:
    """Return the best-available float [0..255] image for the sprite."""
    if "gray" in sprite.meta:
        return sprite.meta["gray"].astype(np.float32)
    return (sprite.pixels.astype(np.float32) * 255.0)


def _attach_gray(sprite: SpriteData, gray: np.ndarray) -> SpriteData:
    """Return a new SpriteData where pixels = threshold(gray @ 128) and
    gray is attached in meta for downstream stages."""
    new_meta = dict(sprite.meta)
    new_meta["gray"] = gray
    return SpriteData(pixels=gray >= 128, meta=new_meta)


@register_stage("autocrop")
def autocrop(sprite: SpriteData, *, threshold: int = 16) -> SpriteData:
    """Crop the sprite to the bounding box of its lit region.

    Works on grayscale (meta['gray']) if available, otherwise on
    .pixels. Useful as the very first stage on source images with
    black matting around the figure.
    """
    gray = _gray_or_pixels(sprite)
    mask = gray >= threshold
    if not mask.any():
        return sprite
    rows = np.where(mask.any(axis=1))[0]
    cols = np.where(mask.any(axis=0))[0]
    y0, y1 = int(rows.min()), int(rows.max()) + 1
    x0, x1 = int(cols.min()), int(cols.max()) + 1
    cropped_gray = gray[y0:y1, x0:x1]
    return _attach_gray(sprite, cropped_gray)


@register_stage("aspect_fit")
def aspect_fit(sprite: SpriteData, *, width: int, height: int,
               bg: int = 0) -> SpriteData:
    """Pad so the aspect ratio matches (width, height) without distorting.

    Does NOT resize — only pads with `bg` on top/bottom or sides so a
    subsequent `downscale` stage lands at the target aspect.
    """
    gray = _gray_or_pixels(sprite)
    sh, sw = gray.shape
    target_ar = width / height
    src_ar = sw / sh
    if abs(src_ar - target_ar) < 1e-3:
        return _attach_gray(sprite, gray)
    if src_ar > target_ar:
        new_h = int(round(sw / target_ar))
        out = np.full((new_h, sw), float(bg), dtype=np.float32)
        pad = (new_h - sh) // 2
        out[pad:pad + sh, :] = gray
    else:
        new_w = int(round(sh * target_ar))
        out = np.full((sh, new_w), float(bg), dtype=np.float32)
        pad = (new_w - sw) // 2
        out[:, pad:pad + sw] = gray
    return _attach_gray(sprite, out)


@register_stage("pad")
def pad(sprite: SpriteData, *, top: int = 0, bottom: int = 0,
        left: int = 0, right: int = 0, bg: int = 0) -> SpriteData:
    """Add uniform `bg` padding around the sprite."""
    gray = _gray_or_pixels(sprite)
    sh, sw = gray.shape
    new_h = sh + top + bottom
    new_w = sw + left + right
    out = np.full((new_h, new_w), float(bg), dtype=np.float32)
    out[top:top + sh, left:left + sw] = gray
    return _attach_gray(sprite, out)


@register_stage("downscale")
def downscale(sprite: SpriteData, *, width: int, height: int,
              method: str = "lanczos") -> SpriteData:
    """Resize (probably down) to exactly (width, height). Operates on the
    float gray image if present, else on pixels. Returns a SpriteData with
    the resized gray in meta and a coarse >=128 binary placeholder.

    method: lanczos | bilinear | nearest | area
    """
    from PIL import Image
    gray = _gray_or_pixels(sprite)
    arr = gray.astype(np.uint8)
    img = Image.fromarray(arr, mode="L")
    if method == "lanczos":
        resample = Image.LANCZOS
    elif method == "bilinear":
        resample = Image.BILINEAR
    elif method == "bicubic":
        resample = Image.BICUBIC
    elif method == "nearest":
        resample = Image.NEAREST
    elif method == "area":
        resample = Image.BOX
    else:
        raise ValueError(f"unknown downscale method {method!r}")
    small = img.resize((width, height), resample)
    out_gray = np.array(small, dtype=np.float32)
    return _attach_gray(sprite, out_gray)


@register_stage("stretch")
def stretch(sprite: SpriteData, *, width: int, height: int,
            method: str = "lanczos") -> SpriteData:
    """Alias for `downscale`, more accurate when resizing non-proportionally
    (e.g., fitting into a target that ignores aspect)."""
    return downscale(sprite, width=width, height=height, method=method)


@register_stage("resize_to_target")
def resize_to_target(sprite: SpriteData, *, width: int, height: int,
                     method: str = "lanczos",
                     preserve_aspect: bool = True) -> SpriteData:
    """Autocrop → (optional aspect-fit) → downscale to exact (w, h)."""
    out = autocrop(sprite, threshold=16)
    if preserve_aspect:
        out = aspect_fit(out, width=width, height=height)
    return downscale(out, width=width, height=height, method=method)
