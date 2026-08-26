#!/usr/bin/env python3
"""Draw a kind of space's tiles as PLACEHOLDER art.

    python tools/gen_placeholder_tiles.py

TWO TILES, AND THAT IS THE WHOLE SET: the ground, and the rock it is cut out of. A wall here is
a flat block and is meant to be -- what tells the player which way a hole goes is the HOLE, drawn
at the angle it is seen from, and a wall that tried to carry that as well would need a tile for
every way rock can meet floor, forty-seven of them, each a chance to be wrong everywhere at once.
Obstacles and the things standing in a room come later, as their own art.

Authored at 16px, the art's own scale; the world runs at twice it. Replaced by `art/` sources
through tools/art.py when real tiles land.
"""

from pathlib import Path

from PIL import Image, ImageDraw

CELL = 16
COLS = 2

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "tilesets" / "descent.png"

FLOOR = (42, 42, 50)
FLOOR_SPECK = (36, 36, 44)
WALL = (19, 19, 24)


def main():
    sheet = Image.new("RGBA", (COLS * CELL, CELL), (0, 0, 0, 0))
    draw = ImageDraw.Draw(sheet)

    # 0: the ground. Speckled, because a floor of one flat colour reads as a hole in the screen
    # rather than as a surface -- a few darker pixels are enough to say there is something there.
    draw.rectangle([0, 0, CELL - 1, CELL - 1], fill=FLOOR)
    for x, y in ((3, 4), (11, 2), (6, 11), (13, 9), (1, 13)):
        draw.point((x, y), fill=FLOOR_SPECK)

    # 1: the rock. Flat on purpose.
    draw.rectangle([CELL, 0, 2 * CELL - 1, CELL - 1], fill=WALL)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT)
    print(f"tiles: {OUT.relative_to(ROOT)} -- {COLS} cells of {CELL}px: floor, wall")


if __name__ == "__main__":
    main()
