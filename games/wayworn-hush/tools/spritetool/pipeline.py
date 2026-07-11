"""Run a declarative stage chain over a SpriteData.

A stage entry is either a bare string ("autocrop") or a single-key dict with
kwargs ({"resize": {"width": 32, "height": 64}}). $width/$height placeholders in
kwargs are bound to the sprite's target size at run time.
"""

from __future__ import annotations

from typing import Any, List

from . import stages as _stages  # noqa: F401  (registers stages)
from .core import SpriteData, get_stage


def _bind(value: Any, width: int, height: int) -> Any:
    if value == "$width":
        return width
    if value == "$height":
        return height
    return value


def run_pipeline(
    sprite: SpriteData, chain: List[Any], width: int, height: int
) -> SpriteData:
    for entry in chain:
        if isinstance(entry, str):
            name, kwargs = entry, {}
        elif isinstance(entry, dict) and len(entry) == 1:
            name = next(iter(entry))
            kwargs = {k: _bind(v, width, height) for k, v in entry[name].items()}
        else:
            raise ValueError(f"bad stage entry: {entry!r}")
        sprite = get_stage(name)(sprite, **kwargs)
    return sprite
