#!/usr/bin/env python3
"""Read-only tile-compression preview.

Reads a baked PROGMEM image array out of games/rpg/images.cpp, renders
it back to a PNG, runs greedy tile-extraction (lossless), renders the
tile-reconstructed image as another PNG, and shows the cost delta.

Touches nothing in the build tree. Outputs land in art/_tile_preview/
(art/ is .gitignored so the previews never get committed).

Usage:
    python scripts/_tile_preview.py FOREST
    python scripts/_tile_preview.py TITLE
    python scripts/_tile_preview.py GATE

Use the underscore-prefixed name (`_tile_preview`) to mark this as a
throwaway dev script, not production tooling — it's a diagnostic, not
part of the asset pipeline.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("ERROR: Pillow not installed.\n")
    sys.exit(1)

ROOT     = Path(__file__).resolve().parents[1]
IMAGES   = ROOT / "games" / "rpg" / "images.cpp"
OUT_DIR  = ROOT / "art" / "_tile_preview"

CANVAS_W = 128
CANVAS_H = 64
TILE_W   = 16
TILE_H   = 16
TILES_X  = CANVAS_W // TILE_W
TILES_Y  = CANVAS_H // TILE_H


def extract_image(name: str) -> bytes:
    """Pull the 1024 raw bytes of `<name>_data` out of images.cpp."""
    text = IMAGES.read_text(encoding="utf-8")
    pat  = re.compile(rf"const u8 {re.escape(name)}_data\[1024\] PROGMEM = \{{(.*?)\}};",
                      re.DOTALL)
    m = pat.search(text)
    if not m:
        raise SystemExit(f"ERROR: {name}_data not found in images.cpp")
    return bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1)))


def bytes_to_image(data: bytes) -> Image.Image:
    """Decode page-major framebuffer bytes back to a 128×64 1-bit PIL image."""
    img = Image.new("1", (CANVAS_W, CANVAS_H), 0)
    px  = img.load()
    for page in range(CANVAS_H // 8):
        for x in range(CANVAS_W):
            byte = data[page * CANVAS_W + x]
            for bit in range(8):
                if byte & (1 << bit):
                    px[x, page * 8 + bit] = 255
    return img


def encode_tile(img: Image.Image, x0: int, y0: int) -> bytes:
    """Pack a 16×16 region as 32 bytes (2 pages × 16 column-bytes each)."""
    out = bytearray(32)
    px  = img.load()
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
    return [encode_tile(img, c * TILE_W, r * TILE_H)
            for r in range(TILES_Y)
            for c in range(TILES_X)]


def reconstruct_from_tiles(palette: list[bytes], layout: list[int]) -> Image.Image:
    """Render the tile palette + layout back to a 128×64 image. Greedy
    tile extraction is byte-exact, so this is bit-identical to the source."""
    img = Image.new("1", (CANVAS_W, CANVAS_H), 0)
    px  = img.load()
    for slot, tile_idx in enumerate(layout):
        sx = (slot % TILES_X) * TILE_W
        sy = (slot // TILES_X) * TILE_H
        tile = palette[tile_idx]
        for page in range(2):
            for c in range(TILE_W):
                byte = tile[page * TILE_W + c]
                for bit in range(8):
                    if byte & (1 << bit):
                        px[sx + c, sy + page * 8 + bit] = 255
    return img


def side_by_side(a: Image.Image, b: Image.Image, gap: int = 4) -> Image.Image:
    """Stitch two images horizontally with a thin separator strip."""
    w, h    = a.size
    canvas  = Image.new("1", (w * 2 + gap, h), 0)
    canvas.paste(a, (0, 0))
    canvas.paste(b, (w + gap, 0))
    # Draw a 1-px white separator down the gap.
    px = canvas.load()
    for y in range(h):
        px[w + gap // 2, y] = 255
    return canvas


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name", help="image name (FOREST | TITLE | GATE)")
    args = ap.parse_args()

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    raw    = extract_image(args.name)
    if len(raw) != 1024:
        raise SystemExit(f"ERROR: extracted {len(raw)} B (expected 1024)")
    img    = bytes_to_image(raw)
    tiles  = slice_tiles(img)

    # Greedy palette: every unique tile gets one slot.
    palette: list[bytes] = []
    index_of: dict[bytes, int] = {}
    layout: list[int] = []
    for t in tiles:
        if t not in index_of:
            index_of[t] = len(palette)
            palette.append(t)
        layout.append(index_of[t])

    counts        = Counter(tiles)
    n_unique      = len(palette)
    raw_bytes     = 1024
    palette_bytes = n_unique * 32
    layout_bytes  = 32
    tiled_total   = palette_bytes + layout_bytes
    delta         = raw_bytes - tiled_total

    rebuilt = reconstruct_from_tiles(palette, layout)
    diff    = sum(1 for a, b in zip(raw, slice_tiles_to_bytes(rebuilt)) if a != b)

    # Save outputs.
    orig_path     = OUT_DIR / f"{args.name}_orig.png"
    rebuilt_path  = OUT_DIR / f"{args.name}_tiled.png"
    compare_path  = OUT_DIR / f"{args.name}_compare.png"
    img.save(orig_path)
    rebuilt.save(rebuilt_path)
    side_by_side(img, rebuilt).save(compare_path)

    print(f"# {args.name}_data — read-only tile preview")
    print(f"# {n_unique} unique tiles / 32 slots ({100 - n_unique * 100 // 32}% reuse)")
    print(f"# top reused: " + ", ".join(
        f"{c}× tile@idx{i}" for i, (_, c) in enumerate(counts.most_common(3))))
    print()
    print(f"# Storage cost (greedy, lossless):")
    print(f"#   raw    : {raw_bytes:>4} B")
    print(f"#   palette: {palette_bytes:>4} B  ({n_unique} × 32)")
    print(f"#   layout : {layout_bytes:>4} B")
    print(f"#   total  : {tiled_total:>4} B   → {'SAVES' if delta > 0 else 'COSTS'} {abs(delta)} B")
    print()
    print(f"# Per-byte diff between original and tile-rebuilt: {diff} (should be 0)")
    print()
    print(f"# Output files:")
    print(f"#   {orig_path.relative_to(ROOT).as_posix()}")
    print(f"#   {rebuilt_path.relative_to(ROOT).as_posix()}")
    print(f"#   {compare_path.relative_to(ROOT).as_posix()}  ← side-by-side")
    return 0


def slice_tiles_to_bytes(img: Image.Image) -> bytes:
    """Re-encode an image as the original 1024-byte page-major form so we
    can verify the tile reconstruction matches the source byte-for-byte."""
    out = bytearray(1024)
    px  = img.load()
    for page in range(CANVAS_H // 8):
        for x in range(CANVAS_W):
            byte = 0
            for bit in range(8):
                if px[x, page * 8 + bit] != 0:
                    byte |= 1 << bit
            out[page * CANVAS_W + x] = byte
    return bytes(out)


if __name__ == "__main__":
    sys.exit(main())
