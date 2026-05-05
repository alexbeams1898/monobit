"""Grayscale filter stages: Gaussian blur, Sobel edge strength,
morphological dilate/erode on binary.

Each stage operates on the float gray image (meta['gray']) where
applicable. Stages that care about a binary mask derive it at the
start from sprite.pixels.
"""

from __future__ import annotations

import numpy as np

from ..core import SpriteData
from ..pipeline import register_stage


def _gray(sprite: SpriteData) -> np.ndarray:
    if "gray" in sprite.meta:
        return sprite.meta["gray"].astype(np.float32)
    return sprite.pixels.astype(np.float32) * 255.0


def _attach_gray(sprite: SpriteData, gray: np.ndarray) -> SpriteData:
    new_meta = dict(sprite.meta)
    new_meta["gray"] = gray
    return SpriteData(pixels=gray >= 128, meta=new_meta)


@register_stage("gaussian_blur")
def gaussian_blur(sprite: SpriteData, *, radius: float = 1.0,
                  radius_ratio: float | None = None) -> SpriteData:
    """Gaussian blur applied to the gray image.

    If `radius_ratio` is given, actual radius = ratio * source_height / 50.
    This lets a pipeline use "0.5" as a scale-invariant blur strength
    regardless of whether the source is 80px tall or 1200px tall.
    """
    from PIL import Image, ImageFilter
    gray = _gray(sprite)
    h, w = gray.shape
    r = radius_ratio * (h / 50.0) if radius_ratio is not None else radius
    if r <= 0:
        return _attach_gray(sprite, gray)
    img = Image.fromarray(gray.astype(np.uint8), mode="L")
    img = img.filter(ImageFilter.GaussianBlur(radius=r))
    return _attach_gray(sprite, np.array(img, dtype=np.float32))


@register_stage("sobel_edges")
def sobel_edges(sprite: SpriteData, *, attach_as: str = "edges") -> SpriteData:
    """Compute per-pixel edge strength via Sobel. Attaches the result as
    meta[attach_as] for downstream threshold stages to use.

    Does NOT modify the pixels or gray; this is a sidecar signal.
    """
    gray = _gray(sprite)
    # 3x3 Sobel kernels
    kx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], dtype=np.float32)
    ky = np.array([[-1, -2, -1], [0, 0, 0], [1, 2, 1]], dtype=np.float32)
    gx = _convolve2d(gray, kx)
    gy = _convolve2d(gray, ky)
    mag = np.sqrt(gx * gx + gy * gy)
    # Normalize to 0..255
    if mag.max() > 0:
        mag = mag * (255.0 / mag.max())
    new_meta = dict(sprite.meta)
    new_meta[attach_as] = mag
    return SpriteData(pixels=sprite.pixels.copy(), meta=new_meta)


def _convolve2d(arr: np.ndarray, kernel: np.ndarray) -> np.ndarray:
    """Simple 2-D valid convolution with zero-padded edges. Kept dependency-
    free; calls that need speed can swap in scipy.signal.convolve2d later."""
    kh, kw = kernel.shape
    pad_h = kh // 2
    pad_w = kw // 2
    padded = np.pad(arr, ((pad_h, pad_h), (pad_w, pad_w)), mode="edge")
    out = np.zeros_like(arr, dtype=np.float32)
    for y in range(arr.shape[0]):
        for x in range(arr.shape[1]):
            region = padded[y:y + kh, x:x + kw]
            out[y, x] = (region * kernel).sum()
    return out


@register_stage("dilate")
def dilate(sprite: SpriteData, *, iterations: int = 1) -> SpriteData:
    """Binary dilation: any False pixel adjacent (8-neigh) to a True
    becomes True. Expands the lit region by `iterations` pixels."""
    px = sprite.pixels.copy()
    for _ in range(iterations):
        px = _dilate_once(px)
    return sprite.with_pixels(px)


def _dilate_once(mask: np.ndarray) -> np.ndarray:
    padded = np.pad(mask, 1, mode="constant", constant_values=False)
    out = (
        padded[1:-1, 1:-1]
        | padded[:-2, 1:-1] | padded[2:, 1:-1]
        | padded[1:-1, :-2] | padded[1:-1, 2:]
        | padded[:-2, :-2]  | padded[:-2, 2:]
        | padded[2:,  :-2]  | padded[2:, 2:]
    )
    return out


@register_stage("erode")
def erode(sprite: SpriteData, *, iterations: int = 1) -> SpriteData:
    """Binary erosion: a True pixel stays True only if ALL 8 neighbors
    are True. Shrinks the lit region by `iterations` pixels."""
    px = sprite.pixels.copy()
    for _ in range(iterations):
        px = _erode_once(px)
    return sprite.with_pixels(px)


def _erode_once(mask: np.ndarray) -> np.ndarray:
    padded = np.pad(mask, 1, mode="constant", constant_values=False)
    out = (
        padded[1:-1, 1:-1]
        & padded[:-2, 1:-1] & padded[2:, 1:-1]
        & padded[1:-1, :-2] & padded[1:-1, 2:]
        & padded[:-2, :-2]  & padded[:-2, 2:]
        & padded[2:,  :-2]  & padded[2:, 2:]
    )
    return out
