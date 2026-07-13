#!/usr/bin/env python3
"""Tileset scaler -- prepares a CC0 source sheet as an LDtk-ready 32px tileset.

The ArMM1998 sheet is autotile-structured (3x3 edged terrain blocks, multi-tile
props), which is LDtk's strength: LDtk owns the edge/corner rules and prop stamps
(see docs/design/MAP-PIPELINE.md). So this tool does NOT curate individual cells;
it scales the WHOLE sheet x2 nearest-neighbor (16px -> 32px, integer/crisp) and
snaps to the GBA RGB555 grid (register consistency with SPRITE-PIPELINE.md). The
result is one tileset PNG whose 32px grid LDtk imports; behavior (walkable, etc.)
is authored in LDtk (IntGrid) + the engine tile-definition table, not here.

Pure transform: same input -> same output. Unit-tested in tests/.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

SCALE = 2  # 16px source -> 32px tiles (integer nearest = crisp; leans EarthBound)


def rgb555_snap(arr: np.ndarray) -> np.ndarray:
    """Snap RGB to the GBA 15-bit grid (32 levels/channel); alpha untouched.
    Mirrors tools/spritetool/stages/palette.py so tiles + sprites share a register."""
    out = arr.copy()
    rgb = out[..., :3].astype(np.float32)
    levels = np.rint(rgb / 255.0 * 31.0)
    out[..., :3] = np.rint(levels / 31.0 * 255.0).astype(np.uint8)
    return out


def scale_sheet(src: Image.Image) -> Image.Image:
    """Whole-sheet x2 nearest + RGB555 snap. Dimensions must stay a whole-tile grid
    so LDtk's 32px cells line up (source is a clean 16px grid -> x2 keeps it clean)."""
    rgba = src.convert("RGBA")
    up = rgba.resize((rgba.width * SCALE, rgba.height * SCALE), Image.NEAREST)
    return Image.fromarray(rgb555_snap(np.array(up)), "RGBA")


def main():
    ap = argparse.ArgumentParser(description="Scale a CC0 sheet to a 32px LDtk tileset.")
    ap.add_argument("source", help="source sheet PNG (16px grid)")
    ap.add_argument("--out", required=True, help="output 32px tileset PNG")
    args = ap.parse_args()

    src = Image.open(args.source)
    if src.width % 16 or src.height % 16:
        raise SystemExit(f"source {src.size} is not a whole 16px grid")
    out = scale_sheet(src)
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    out.save(args.out)
    print(f"wrote {args.out} ({out.size[0]}x{out.size[1]}, "
          f"{out.size[0] // 32}x{out.size[1] // 32} tiles @32)")


if __name__ == "__main__":
    main()
