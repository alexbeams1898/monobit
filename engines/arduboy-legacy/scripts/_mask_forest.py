#!/usr/bin/env python3
"""Re-bake FOREST_LZ77_data with plaque + HUD regions zeroed.

draw_main_menu() decodes the entire 1024 B forest into fb::buffer, then
immediately overpaints two big regions:

  1. HUD strip y=0..8 (top 9 rows, full width) — `fb::clear_rect(0,0,128,9)`
  2. SELVA OSCURA plaque x=28..99, y=11..61 — `fb::clear_rect(PLAQUE_*)`

Pixels under those regions never display. Storing them costs flash for
nothing. Zeroing the bytes at bake time and re-LZ77-ing produces long
zero runs that LZ77 compresses to almost nothing — saved ~361 B flash
the first time we ran this (709 -> 348 B).

Usage: re-run after changing draw_main_menu's plaque geometry constants.

    python scripts/_mask_forest.py            # measure only
    python scripts/_mask_forest.py --bake     # rewrite images.cpp in place
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import _lz77_bake as lb  # noqa: E402

IMAGES = ROOT / "games" / "rpg" / "images.cpp"

# Layout constants — keep in lockstep with draw_main_menu in games/rpg/game.cpp.
HUD_TOP_ROWS  = 9               # y=0..8 cleared by clear_rect(0,0,WIDTH,9)
PLAQUE_X      = 28
PLAQUE_W      = 72              # x=28..99
PLAQUE_Y      = 11
PLAQUE_H      = 51              # y=11..61
FB_WIDTH      = 128
FB_HEIGHT     = 64
FB_PAGES      = FB_HEIGHT // 8  # 8


def mask_overpainted(raw: bytes) -> bytes:
    """Zero every pixel that draw_main_menu's overpaint will clear."""
    buf = bytearray(raw)
    # HUD strip: y=0..(HUD_TOP_ROWS-1)
    hud_end_y = HUD_TOP_ROWS
    for y in range(hud_end_y):
        page = y // 8
        bit  = y % 8
        for x in range(FB_WIDTH):
            buf[page * FB_WIDTH + x] &= ~(1 << bit) & 0xFF
    # Plaque: x in [PLAQUE_X, PLAQUE_X+PLAQUE_W), y in [PLAQUE_Y, PLAQUE_Y+PLAQUE_H)
    for y in range(PLAQUE_Y, PLAQUE_Y + PLAQUE_H):
        page = y // 8
        bit  = y % 8
        for x in range(PLAQUE_X, PLAQUE_X + PLAQUE_W):
            buf[page * FB_WIDTH + x] &= ~(1 << bit) & 0xFF
    return bytes(buf)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bake", action="store_true",
                    help="rewrite games/rpg/images.cpp's FOREST_LZ77_data in place")
    args = ap.parse_args()

    text = IMAGES.read_text(encoding="utf-8")
    pat = re.compile(
        r'// LZ77-compressed via[^\n]*\n(?:[^\n]*\n)*?const u8 FOREST_LZ77_data\[(\d+)\] PROGMEM = \{(.*?)\};',
        re.DOTALL,
    )
    m = pat.search(text)
    if not m:
        print("ERROR: FOREST_LZ77_data block not found in images.cpp")
        return 1

    old_size = int(m.group(1))
    encoded  = bytes(int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', m.group(2)))
    raw      = lb.decode(encoded)
    if len(raw) != 1024:
        print(f"ERROR: decoded {len(raw)} B (expected 1024)")
        return 1

    masked   = mask_overpainted(raw)
    new      = lb.encode(masked)
    verify   = lb.decode(new)
    if verify != masked:
        print("ERROR: round-trip mismatch")
        return 1

    print(f"FOREST_LZ77_data: {old_size} B -> {len(new)} B  (saves {old_size - len(new)} B)")

    if not args.bake:
        return 0

    lines = [
        "// LZ77-compressed via scripts/_lz77_bake.py. Decode with lz77::decode().",
        f"// Compressed: {len(new)} B vs raw 1024 B; saves {1024 - len(new)} B of flash.",
        "// PRE-MASKED: pixels under the SELVA OSCURA plaque (x=28..99, y=11..61)",
        "// and the HUD strip (y=0..8) are zeroed at bake time. draw_main_menu",
        "// overpaints those regions every frame anyway, so storing them costs",
        "// flash for nothing. Long zero runs LZ77-compress to almost nothing.",
        "// If the plaque geometry changes, re-bake via scripts/_mask_forest.py.",
        f"const u8 FOREST_LZ77_data[{len(new)}] PROGMEM = {{",
    ]
    for i in range(0, len(new), 16):
        chunk = new[i:i + 16]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")

    patched = pat.sub("\n".join(lines), text, count=1)
    IMAGES.write_text(patched, encoding="utf-8")
    print(f"Wrote {IMAGES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
