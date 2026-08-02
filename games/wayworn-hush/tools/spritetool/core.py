"""Core types for the color sprite pipeline.

A SpriteData is an RGBA uint8 image plus a metadata sidecar. Stages are pure
functions SpriteData -> SpriteData, registered by name so the manifest can chain
them declaratively. Ported from the Arduboy spritebake tool's stage-registry
design, with the 1-bit boolean core replaced by a color RGBA array.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable, Dict

import numpy as np


@dataclass
class SpriteData:
    # (H, W, 4) uint8, RGBA. Alpha is treated as 1-bit (0 or 255) to match GBA
    # sprite transparency; stages should not introduce partial alpha.
    pixels: np.ndarray
    meta: dict = field(default_factory=dict)

    def __post_init__(self) -> None:
        if self.pixels.dtype != np.uint8:
            raise TypeError(f"SpriteData.pixels must be uint8, got {self.pixels.dtype}")
        if self.pixels.ndim != 3 or self.pixels.shape[2] != 4:
            raise ValueError(f"SpriteData.pixels must be (H,W,4) RGBA, got {self.pixels.shape}")

    @property
    def height(self) -> int:
        return self.pixels.shape[0]

    @property
    def width(self) -> int:
        return self.pixels.shape[1]

    def copy(self) -> "SpriteData":
        return SpriteData(self.pixels.copy(), dict(self.meta))


Stage = Callable[..., SpriteData]

# Global registry: stage name -> function. A stage takes (SpriteData, **kwargs)
# and returns a new SpriteData.
STAGES: Dict[str, Stage] = {}


def register_stage(name: str) -> Callable[[Stage], Stage]:
    def deco(fn: Stage) -> Stage:
        if name in STAGES:
            raise ValueError(f"stage already registered: {name}")
        STAGES[name] = fn
        return fn

    return deco


def get_stage(name: str) -> Stage:
    if name not in STAGES:
        raise KeyError(f"unknown stage '{name}' (registered: {sorted(STAGES)})")
    return STAGES[name]
