#!/usr/bin/env python3
"""Split circlebackgroundsinorder.png into 9 circle-background tiles and emit
a single compressed C file.

The atlas is read row-major 3x3: Limbo (circle 0) at top-left, Treachery
(circle 8) at bottom-right.

Pipeline per tile:
  1. Crop from the atlas
  2. Resize to 128x64 (Lanczos)
  3. Atkinson dither to 1-bit
  4. Apply a 2x2 checker mask ((x+y) even pixels forced off). Halves
     perceived intensity and keeps 1-bit sprites legible against it.
  5. **Nibble-pack**: because the checker mask GUARANTEES every byte has
     exactly 4 zero bit-positions (0xAA or 0x55 depending on column
     parity), we compress each byte into a 4-bit nibble without loss.
     Two columns (x even + x odd) pack into one byte. Tile size drops
     from 1024 -> 512 bytes.
  6. **Zero-run + literal-run RLE** on the nibble-packed bytes. Tag byte
     with high bit set encodes a zero run of (low 7 bits); high bit clear
     encodes a literal run of (low 7 bits) followed by the literals.
     Brings the 9 tiles from ~9216 B to ~4200 B combined.

Decoder lives in games/rpg/backgrounds.cpp (released once per wave start
into a 1 KB RAM scratch buffer; frame render blits the scratch directly).

Usage (from repo root):
    python scripts/convert_circle_backgrounds.py \\
        --in art/circlebackgroundsinorder.png \\
        --out games/rpg/backgrounds.cpp
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("ERROR: Pillow not installed.\n")
    sys.exit(1)

# Full-resolution title-card tiles. Stored INVERTED (white architecture
# becomes black, black background becomes lit) so the cards read as
# "ink-on-page" Hell — recognizable trees, pillars, rings of Cocytus.
# Used only on the brief CIRCLE_CARD transition between circles, never
# during active gameplay (the playfield stays plain black).
#
# Three tiles only: LIMBO (upper hell), VIOLENCE (middle hell), TREACHERY
# (lower hell). The 9 game circles map onto these via CIRCLE_TO_CARD in
# games/rpg/game.cpp — a tripartite split that mirrors Dante's own
# upper/middle/lower division of the Inferno.
TILE_W = 128
TILE_H = 64
ATLAS_W = 3
ATLAS_H = 3
# Indices into the source atlas (row-major); names are for the comments.
SELECTED_ATLAS_INDICES = [0, 6, 8]  # LIMBO, VIOLENCE, TREACHERY
SELECTED_NAMES = ["LIMBO", "VIOLENCE", "TREACHERY"]
TILES = len(SELECTED_ATLAS_INDICES)
# Kept for historic logging in main(); the actual labels emitted are
# SELECTED_NAMES above.
CIRCLES = SELECTED_NAMES


def box_threshold(img: Image.Image, thresh: int = 48) -> Image.Image:
    """Strategy E — box-average downsample + threshold, no dither, no mask.

    The source art is already near-1-bit stipple; we want to preserve the
    densest shapes as-is. Lower threshold = more ink kept = denser / more
    architectural-looking result.
    """
    t = img.resize((TILE_W, TILE_H), Image.BOX)
    return t.point(lambda p: 255 if p >= thresh else 0, mode="1")




def tile_to_framebuffer(img: Image.Image) -> bytes:
    """Page-major 1-bit pack (same convention as png_to_image.py)."""
    px = img.load()
    out = bytearray(TILE_W * TILE_H // 8)
    for page in range(TILE_H // 8):
        for x in range(TILE_W):
            byte = 0
            for bit in range(8):
                y = page * 8 + bit
                if px[x, y] != 0:
                    byte |= 1 << bit
            out[page * TILE_W + x] = byte
    return bytes(out)


def rle_encode(data: bytes) -> bytes:
    """Zero-run + literal-run RLE.

    Tag byte layout:
      0b1RRRRRRR  -> zero run of R bytes (1..127)
      0b0LLLLLLL  -> literal run of L bytes (1..127), followed by L bytes
    End marker: a zero tag (0x00) means "end of stream" — emitted once.
    """
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        if data[i] == 0:
            run = 1
            while i + run < n and data[i + run] == 0 and run < 127:
                run += 1
            out.append(0x80 | run)
            i += run
        else:
            start = i
            while i < n and data[i] != 0 and (i - start) < 127:
                i += 1
            run = i - start
            out.append(run)
            out.extend(data[start:start + run])
    out.append(0x00)  # end-of-stream sentinel
    return bytes(out)


def split_atlas(atlas: Image.Image) -> list[Image.Image]:
    """Slice the 3x3 source atlas into the SELECTED_ATLAS_INDICES tiles only,
    in their selected order. Returns L-mode tiles already resized to 128x64.
    """
    aw, ah = atlas.size
    cell_w = aw // ATLAS_W
    cell_h = ah // ATLAS_H
    tiles: list[Image.Image] = []
    for idx in SELECTED_ATLAS_INDICES:
        row, col = divmod(idx, ATLAS_W)
        box = (col * cell_w, row * cell_h, (col + 1) * cell_w, (row + 1) * cell_h)
        tile = atlas.crop(box).convert("L")
        tile = tile.resize((TILE_W, TILE_H), Image.LANCZOS)
        tiles.append(tile)
    return tiles


def emit(tiles: list[bytes]) -> str:
    """Emit one concatenated PROGMEM byte array of raw 64x32 tile data,
    plus an OFFSETS[] index. Each tile is 256 bytes (64 cols x 4 pages).
    The render() decoder pixel-doubles to 128x64 on the fly.
    """
    offsets = []
    flat = bytearray()
    for blob in tiles:
        offsets.append(len(flat))
        flat.extend(blob)
    total = len(flat)

    lines = [
        "// Auto-generated by scripts/convert_circle_backgrounds.py — do not edit.",
        "// Three CIRCLE_CARD images at 128x64 (LIMBO, VIOLENCE, TREACHERY),",
        "// pre-inverted (white architecture on black). The 9 game circles map",
        "// onto these via game.cpp's CIRCLE_TO_CARD lookup — a tripartite",
        "// split mirroring Dante's upper / middle / lower Hell. Shown only",
        "// during the brief CIRCLE_CARD transition state; never used during",
        "// active gameplay.",
        "",
        "#include \"backgrounds.h\"",
        "",
        "#include \"framebuffer.h\"",
        "#include \"progmem.h\"",
        "",
        "namespace backgrounds {",
        "",
        "namespace {",
        "",
        f"// {TILES} tiles, 1024 bytes each = {total} B total.",
        f"const u8 STREAM[{total}] PROGMEM = {{",
    ]
    for j in range(0, total, 16):
        chunk = flat[j:j + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    lines.append(f"// Per-tile start offsets into STREAM[].")
    lines.append(f"const u16 OFFSETS[{TILES}] PROGMEM = {{")
    lines.append("    " + ", ".join(str(o) for o in offsets) + ",")
    lines.append("};")
    lines.append("")
    lines.append("}  // namespace")
    lines.append("")
    lines.append("// Blit one full-resolution card straight from PROGMEM into")
    lines.append("// `out` (1024 bytes, page-major). `card` is 0..CARD_COUNT-1, NOT")
    lines.append("// a circle index — call it via CIRCLE_TO_CARD[] in game.cpp.")
    lines.append("// Used only by the CIRCLE_CARD transition state; never called")
    lines.append("// during active gameplay.")
    lines.append("void render(u8 card, u8* out) {")
    lines.append(f"  if (card >= {TILES}) {{")
    lines.append("    for (u16 i = 0; i < 1024; ++i) out[i] = 0;")
    lines.append("    return;")
    lines.append("  }")
    lines.append("  const u16 src = pgm_read_word(&OFFSETS[card]);")
    lines.append("  for (u16 i = 0; i < 1024; ++i) {")
    lines.append("    out[i] = pgm_read_byte(&STREAM[src + i]);")
    lines.append("  }")
    lines.append("}")
    lines.append("")
    lines.append("}  // namespace backgrounds")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="src", type=Path, required=True)
    ap.add_argument("--out", dest="dst", type=Path, required=True)
    args = ap.parse_args()

    if not args.src.exists():
        sys.stderr.write(f"ERROR: atlas not found: {args.src}\n")
        return 1

    atlas = Image.open(args.src).convert("L")
    tiles = split_atlas(atlas)
    rle_blobs: list[bytes] = []
    raw_total = 0
    rle_total = 0
    blobs: list[bytes] = []
    for i, tile in enumerate(tiles):
        shaped = box_threshold(tile)
        fb_bytes = tile_to_framebuffer(shaped)
        # Invert: art is mostly-lit; XOR flips so the cards display as
        # white architecture on black ground (the look approved in
        # art/_preview_v2/atlas_E_box_thresh_low_INVERTED.png).
        fb_inv = bytes(b ^ 0xFF for b in fb_bytes)
        blobs.append(fb_inv)
        raw_total += len(fb_inv)
        print(f"  {CIRCLES[i]}: raw={len(fb_inv)}")

    out_text = emit(blobs)
    args.dst.write_text(out_text, encoding="utf-8")
    print(f"Wrote {args.dst}")
    print(f"  total:            {raw_total} B (raw, no compression)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
