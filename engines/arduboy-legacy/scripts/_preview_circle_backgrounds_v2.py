#!/usr/bin/env python3
"""Experimental preview — try several conversion strategies for the
circle atlas. Writes each variant as its own preview PNG so we can eyeball
which actually looks like the source art rather than a starfield.

Strategies tried, per tile:
  A) current pipeline: Lanczos resize + Atkinson dither + checker mask
  B) Lanczos resize + threshold (no dither) + no checker
  C) NEAREST resize + threshold + no checker
  D) NEAREST resize + threshold + 50%-of-lit checker (lit pixels dropped half the time)
  E) box-average resize + threshold at 64 (lower = keeps more ink) + no checker

Outputs go to art/_preview_v2/.
"""
from __future__ import annotations
from pathlib import Path
from PIL import Image
import sys

SRC = Path(__file__).resolve().parents[1] / "art" / "circlebackgroundsinorder.png"
OUT = Path(__file__).resolve().parents[1] / "art" / "_preview_v2"
SCALE = 4
TILE_W = 128
TILE_H = 64

CIRCLES = [
    "LIMBO", "LUST", "GLUTTONY",
    "GREED", "WRATH", "HERESY",
    "VIOLENCE", "FRAUD", "TREACHERY",
]


def split(atlas: Image.Image) -> list[Image.Image]:
    aw, ah = atlas.size
    cw, ch = aw // 3, ah // 3
    out = []
    for row in range(3):
        for col in range(3):
            out.append(atlas.crop((col*cw, row*ch, (col+1)*cw, (row+1)*ch)).convert("L"))
    return out


def atkinson(img):
    w, h = img.size
    buf = [[float(img.getpixel((x, y))) for x in range(w)] for y in range(h)]
    out = Image.new("1", (w, h), 0)
    for y in range(h):
        for x in range(w):
            old = buf[y][x]
            new = 255.0 if old >= 128 else 0.0
            buf[y][x] = new
            err = (old - new) / 8.0
            for dx, dy in [(1,0),(2,0),(-1,1),(0,1),(1,1),(0,2)]:
                nx, ny = x+dx, y+dy
                if 0 <= nx < w and 0 <= ny < h:
                    buf[ny][nx] += err
    px = out.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if buf[y][x] >= 128 else 0
    return out


def checker_drop_half(img):
    px = img.load()
    w, h = img.size
    for y in range(h):
        for x in range(w):
            if ((x + y) & 1) == 0:
                px[x, y] = 0
    return img


def strategy_A(tile):
    t = tile.resize((TILE_W, TILE_H), Image.LANCZOS)
    t = atkinson(t)
    checker_drop_half(t)
    return t


def strategy_B(tile):
    t = tile.resize((TILE_W, TILE_H), Image.LANCZOS)
    return t.point(lambda p: 255 if p > 128 else 0, mode="1")


def strategy_C(tile):
    t = tile.resize((TILE_W, TILE_H), Image.NEAREST)
    return t.point(lambda p: 255 if p > 64 else 0, mode="1")


def strategy_D(tile):
    t = tile.resize((TILE_W, TILE_H), Image.NEAREST)
    b = t.point(lambda p: 255 if p > 64 else 0, mode="1")
    checker_drop_half(b)
    return b


def strategy_E(tile):
    t = tile.resize((TILE_W, TILE_H), Image.BOX)
    return t.point(lambda p: 255 if p > 48 else 0, mode="1")


STRATEGIES = [
    ("A_current", strategy_A),
    ("B_lanczos_thresh", strategy_B),
    ("C_nearest_thresh", strategy_C),
    ("D_nearest_thresh_checker", strategy_D),
    ("E_box_thresh_low", strategy_E),
]


def scale_up(img):
    return img.convert("L").resize((TILE_W * SCALE, TILE_H * SCALE), Image.NEAREST)


def main() -> int:
    if not SRC.exists():
        sys.stderr.write(f"no source: {SRC}\n")
        return 1
    OUT.mkdir(exist_ok=True, parents=True)
    atlas = Image.open(SRC).convert("L")
    tiles = split(atlas)

    # One combined image per strategy showing the 9 tiles in 3x3.
    gap = 8
    w = TILE_W * SCALE
    h = TILE_H * SCALE
    for name, fn in STRATEGIES:
        combined = Image.new("L", (3*w + 4*gap, 3*h + 4*gap), 64)
        for i, tile in enumerate(tiles):
            img = scale_up(fn(tile))
            row, col = divmod(i, 3)
            x = gap + col * (w + gap)
            y = gap + row * (h + gap)
            combined.paste(img, (x, y))
        combined.save(OUT / f"atlas_{name}.png")
        print(f"wrote {OUT / f'atlas_{name}.png'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
