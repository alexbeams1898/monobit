"""Assemble per-direction, per-state frames into the engine's sprite-sheet
layout and emit the matching animation manifest JSON.

Engine layout (see engines/engine/docs/ENGINE.md 'Animation system'):
  Rows = states.  Column = dir_index * max_frames_per_state + frame_index.
  Direction order: South=0, West=1, East=2, North=3.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List

import numpy as np

from .core import SpriteData

DIRECTIONS = ["south", "west", "east", "north"]  # index 0..3, engine order


@dataclass
class StateSpec:
    name: str
    frames: int
    duration: float
    row: int


def assemble_sheet(
    frames: Dict[str, Dict[str, List[SpriteData]]],
    frame_w: int,
    frame_h: int,
    direction_count: int,
    states: List[StateSpec],
) -> np.ndarray:
    """frames[state_name][direction] = list of per-frame SpriteData.
    Returns the assembled RGBA sheet."""
    max_frames = max((s.frames for s in states), default=1)
    cols = direction_count * max_frames
    rows = len(states)
    sheet = np.zeros((rows * frame_h, cols * frame_w, 4), dtype=np.uint8)

    for s in states:
        for di in range(direction_count):
            dname = DIRECTIONS[di]
            dframes = frames.get(s.name, {}).get(dname, [])
            for fi in range(s.frames):
                if fi >= len(dframes):
                    continue
                cell = dframes[fi].pixels
                if cell.shape[0] != frame_h or cell.shape[1] != frame_w:
                    raise ValueError(
                        f"{s.name}/{dname} frame {fi} is {cell.shape[1]}x{cell.shape[0]}, "
                        f"expected {frame_w}x{frame_h}"
                    )
                col = di * max_frames + fi
                y = s.row * frame_h
                x = col * frame_w
                sheet[y : y + frame_h, x : x + frame_w] = cell
    return sheet


def animation_manifest(
    texture: str,
    frame_w: int,
    frame_h: int,
    direction_count: int,
    states: List[StateSpec],
) -> dict:
    """The { texture, frame_width, ..., states } shape the game's animation
    loader consumes (matches config/player.json)."""
    max_frames = max((s.frames for s in states), default=1)
    return {
        "texture": texture,
        "frame_width": frame_w,
        "frame_height": frame_h,
        "direction_count": direction_count,
        "max_frames_per_state": max_frames,
        "states": {
            s.name: {"row": s.row, "frames": s.frames, "duration": s.duration}
            for s in states
        },
    }
