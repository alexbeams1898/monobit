#!/usr/bin/env python3
"""Tile-extraction diagnostic for 1-bit backgrounds.

Slices a 128×64 1-bit PNG (or any image; thresholds at 128) into 16×16
tiles and reports how many unique tiles it contains. The number tells
you whether the image is a good candidate for tile-palette compression
(see engine/tilemap.h).

Usage
-----
    python scripts/tile_extractor.py art/_cell_check/realforest.png

    python scripts/tile_extractor.py art/foo.png --bake palette.cpp
        Auto-mode bake: emit a `const u8 PALETTE_data[]` and
        `const u8 LAYOUT_data[]` to stdout, ready to paste into a .cpp
        file. Only sensible if the unique-tile count is small enough
        that the palette+layout total is smaller than 1024 B.

    python scripts/tile_extractor.py art/foo.png --palette art/tiles.png
        Mapped mode: source's tiles are matched to the closest tile in
        a hand-authored palette image. Emits the layout against your
        palette. Use this when --bake's auto palette is too big or when
        you want explicit per-tile control.

Cost math
---------
    raw image           = 1024 B always
    tile palette + N layouts
                        = (unique_tiles × 32 B) + (N × 32 B) + ~80 B (renderer once)

    Auto mode "wins" when:
        unique_tiles × 32 + N × 32 + 80 < N × 1024
    For N=1: unique_tiles < 29 to break even
    For N=3: unique_tiles < 93 (always wins for 3+ images)

Output is informational only — this script never writes to the build
tree unless you redirect --bake output yourself.
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("ERROR: Pillow not installed. pacman -S mingw-w64-x86_64-python-pillow\n")
    sys.exit(1)


CANVAS_W = 128
CANVAS_H = 64
TILE_W = 16
TILE_H = 16
TILES_X = CANVAS_W // TILE_W   # 8
TILES_Y = CANVAS_H // TILE_H   # 4
N_SLOTS = TILES_X * TILES_Y    # 32


def to_1bit(img: Image.Image, threshold: int = 128) -> Image.Image:
    """Convert to grayscale then threshold to 1-bit."""
    if img.size != (CANVAS_W, CANVAS_H):
        img = img.resize((CANVAS_W, CANVAS_H), Image.LANCZOS)
    g = img.convert("L")
    return g.point(lambda p: 255 if p >= threshold else 0, mode="1")


def encode_tile(img: Image.Image, x0: int, y0: int) -> bytes:
    """Encode the 16×16 tile starting at (x0, y0) as 32 bytes in the
    engine's column-major page layout (matches draw_sprite_progmem)."""
    out = bytearray(32)
    px = img.load()
    # Two pages of 8 rows each. For each page p (0..1), each column c
    # (0..15) packs 8 vertical pixels into one byte (bit 0 = top).
    for page in range(2):
        for c in range(TILE_W):
            byte = 0
            for bit in range(8):
                y = y0 + page * 8 + bit
                if px[x0 + c, y] != 0:
                    byte |= 1 << bit
            out[page * TILE_W + c] = byte
    return bytes(out)


def slice_tiles(img: Image.Image) -> list[bytes]:
    """Return the 32 tile-encoded bytestrings in row-major order."""
    return [
        encode_tile(img, c * TILE_W, r * TILE_H)
        for r in range(TILES_Y)
        for c in range(TILES_X)
    ]


def emit_c_array(name: str, data: bytes, per_line: int = 16) -> str:
    lines = [f"const u8 {name}[{len(data)}] PROGMEM = {{"]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def auto_analyze(tiles: list[bytes]) -> tuple[list[bytes], list[int]]:
    """Build the auto palette: every unique tile becomes a palette entry,
    in first-seen order. Return (palette, layout_indices)."""
    palette: list[bytes] = []
    index_of: dict[bytes, int] = {}
    layout: list[int] = []
    for t in tiles:
        if t not in index_of:
            index_of[t] = len(palette)
            palette.append(t)
        layout.append(index_of[t])
    return palette, layout


def cost_summary(unique_count: int, n_images: int = 1) -> str:
    raw_total      = n_images * 1024
    tiled_total    = unique_count * 32 + n_images * 32 + 80  # +renderer
    delta          = raw_total - tiled_total
    sign           = "saves" if delta > 0 else "costs"
    return (f"  N={n_images:>2}  raw={raw_total:>5} B  "
            f"tiled={tiled_total:>5} B  → {sign} {abs(delta):>4} B")


def map_to_palette(tiles: list[bytes], palette_tiles: list[bytes]) -> list[int]:
    """For each source tile, pick the palette tile with smallest hamming
    distance. Used in --palette mode."""
    layout: list[int] = []
    for t in tiles:
        best_i = 0
        best_d = 256
        for i, p in enumerate(palette_tiles):
            d = sum(bin(a ^ b).count("1") for a, b in zip(t, p))
            if d < best_d:
                best_d = d
                best_i = i
                if d == 0:
                    break
        layout.append(best_i)
    return layout


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", type=Path, help="source 128×64 image")
    ap.add_argument("--threshold", type=int, default=128,
                    help="1-bit threshold (default 128)")
    ap.add_argument("--bake", action="store_true",
                    help="emit C arrays for the auto palette + layout")
    ap.add_argument("--palette", type=Path, default=None,
                    help="path to hand-authored palette PNG (mapped mode)")
    ap.add_argument("--name", default="SCENE",
                    help="C identifier prefix for --bake output (default SCENE)")
    args = ap.parse_args()

    if not args.png.exists():
        sys.stderr.write(f"ERROR: file not found: {args.png}\n")
        return 1

    img    = to_1bit(Image.open(args.png), args.threshold)
    tiles  = slice_tiles(img)
    counts = Counter(tiles)

    print(f"# {args.png}")
    print(f"# {CANVAS_W}×{CANVAS_H} → {TILES_X}×{TILES_Y} = {N_SLOTS} tile slots")
    print(f"# unique tiles: {len(counts)} / {N_SLOTS}")
    print(f"# tile reuse:   {(1 - len(counts) / N_SLOTS) * 100:.1f}%")
    most_common = counts.most_common(3)
    print(f"# top reused:   " + ", ".join(
        f"{c}× tile@idx{i}" for i, (_, c) in enumerate(most_common)))
    print()
    print("# Cost vs raw 1024-B image:")
    for n in (1, 3, 6, 9):
        print(cost_summary(len(counts), n))
    print()

    if args.palette is not None:
        # Mapped mode: read palette image, slice it into tiles too. Each
        # palette tile is 16×16 — palette image can be any size that's a
        # multiple of 16 in both dimensions.
        pimg = to_1bit(Image.open(args.palette), args.threshold)
        # Palette image's tile count = (w/16) * (h/16). All in row-major.
        pw, ph = pimg.size
        if pw % TILE_W != 0 or ph % TILE_H != 0:
            sys.stderr.write(f"ERROR: palette image {pw}×{ph} is not a multiple of 16\n")
            return 1
        palette_tiles = []
        for r in range(ph // TILE_H):
            for c in range(pw // TILE_W):
                palette_tiles.append(encode_tile(pimg, c * TILE_W, r * TILE_H))
        print(f"# Mapped mode: {len(palette_tiles)} palette tiles from {args.palette}")
        layout = map_to_palette(tiles, palette_tiles)
        if args.bake:
            palette_bytes = b"".join(palette_tiles)
            layout_bytes  = bytes(layout)
            print(emit_c_array(args.name + "_PALETTE", palette_bytes))
            print()
            print(emit_c_array(args.name + "_LAYOUT", layout_bytes))
        return 0

    # Auto mode.
    if args.bake:
        palette, layout = auto_analyze(tiles)
        palette_bytes   = b"".join(palette)
        layout_bytes    = bytes(layout)
        print(emit_c_array(args.name + "_PALETTE", palette_bytes))
        print()
        print(emit_c_array(args.name + "_LAYOUT", layout_bytes))

    return 0


if __name__ == "__main__":
    sys.exit(main())
