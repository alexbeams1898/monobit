"""Core SpriteData type — every stage in the pipeline operates on this.

SpriteData wraps a 2-D NumPy boolean array (H x W, row-major, True = lit).
All pipeline stages are pure functions SpriteData -> SpriteData (plus
config). Keeping this canonical avoids the PIL/numpy/raw-bytes impedance
mismatch that plagued the earlier ad-hoc scripts.

Invariants:
  - .pixels is np.bool_ dtype, 2-D, shape (H, W)
  - .pixels.flags.writeable may be False for frozen intermediates
  - Height & width are always derivable from .pixels.shape; the @property
    forms are provided for readability only.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

import numpy as np

try:
    from PIL import Image
except ImportError as e:  # pragma: no cover
    raise ImportError("spritebake requires Pillow; `pip install pillow`") from e


@dataclass(frozen=True)
class SpriteData:
    """An immutable-by-convention 1-bit sprite."""

    pixels: np.ndarray  # shape (H, W), dtype=bool
    meta: dict = field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.pixels.dtype != np.bool_:
            object.__setattr__(self, "pixels", self.pixels.astype(bool))
        if self.pixels.ndim != 2:
            raise ValueError(
                f"SpriteData pixels must be 2-D, got shape {self.pixels.shape}"
            )

    @property
    def width(self) -> int:
        return int(self.pixels.shape[1])

    @property
    def height(self) -> int:
        return int(self.pixels.shape[0])

    @property
    def shape(self) -> tuple[int, int]:
        return (self.height, self.width)

    def lit_count(self) -> int:
        return int(self.pixels.sum())

    def is_empty(self) -> bool:
        return not self.pixels.any()

    def with_pixels(self, pixels: np.ndarray) -> "SpriteData":
        """Return a new SpriteData with updated pixels, preserving meta."""
        return SpriteData(pixels=pixels, meta=dict(self.meta))

    def with_meta(self, **kwargs) -> "SpriteData":
        """Return a new SpriteData with updated meta entries."""
        merged = dict(self.meta)
        merged.update(kwargs)
        return SpriteData(pixels=self.pixels.copy(), meta=merged)

    def copy(self) -> "SpriteData":
        return SpriteData(pixels=self.pixels.copy(), meta=dict(self.meta))

    # ---- Conversions ----

    @classmethod
    def from_image(cls, img: Image.Image, *, threshold: int | None = None) -> "SpriteData":
        """Create from a PIL image. RGB/RGBA flattened to luminance.

        If threshold is given, pixels >= threshold are lit. Otherwise,
        use Pillow's mode=1 conversion (equal-weight binary).
        """
        if img.mode == "1":
            arr = np.array(img, dtype=bool)
        elif threshold is not None:
            gray = img.convert("L")
            arr = np.array(gray) >= threshold
        else:
            arr = np.array(img.convert("1"), dtype=bool)
        return cls(pixels=arr)

    @classmethod
    def from_ndarray(cls, arr: np.ndarray) -> "SpriteData":
        """Adopt a 2-D array as the sprite; nonzero values become lit."""
        return cls(pixels=np.asarray(arr, dtype=bool))

    @classmethod
    def empty(cls, width: int, height: int) -> "SpriteData":
        return cls(pixels=np.zeros((height, width), dtype=bool))

    def to_image(self) -> Image.Image:
        """Render as a PIL '1'-mode Image (True -> 255, False -> 0)."""
        return Image.fromarray(self.pixels.astype(np.uint8) * 255, mode="L").convert("1")

    def to_upscaled_image(self, scale: int, *, fg: int = 255, bg: int = 0) -> Image.Image:
        """Return an RGB image with each pixel scaled SCALE×SCALE."""
        arr = self.pixels.astype(np.uint8)
        arr = np.where(arr, fg, bg).astype(np.uint8)
        scaled = np.kron(arr, np.ones((scale, scale), dtype=np.uint8))
        return Image.fromarray(scaled, mode="L").convert("RGB")

    # ---- Debug ----

    def ascii(self, lit: str = "#", unlit: str = ".") -> str:
        """Return a simple ASCII-art rendering for logging / debugging."""
        return "\n".join("".join(lit if p else unlit for p in row) for row in self.pixels)

    def __repr__(self) -> str:
        return f"SpriteData({self.width}x{self.height}, lit={self.lit_count()})"


def as_float_gray(img: Image.Image | np.ndarray) -> np.ndarray:
    """Coerce a PIL image or ndarray to a 2-D float array in [0, 255]."""
    if isinstance(img, Image.Image):
        return np.array(img.convert("L"), dtype=np.float32)
    arr = np.asarray(img)
    if arr.ndim == 3:
        arr = np.mean(arr[..., :3], axis=-1)
    return arr.astype(np.float32)