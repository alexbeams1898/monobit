#!/usr/bin/env python3
"""Pack the per-item icons into the shared items sheet.

Item art lives on ONE sheet like every other art in the game (tiles, furniture,
characters), so it is browsable and consistent rather than a folder of loose
files. Each item's config names its cell (`icon_cell: [col, row]`), and this
builds the sheet those cells index.

    python pack_items.py [--icons assets/sprites/items] [--out assets/tilesets/items.png]

Cells are assigned in ALPHABETICAL order of the source filenames and printed, so
a new item's cell is whatever this reports -- paste it into that item's config.
Adding an item re-flows the cells after it alphabetically, so rerun this and
update the configs it names (the tool prints every item's cell every time, and
--check verifies the configs already agree).
"""

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

CELL = 32
COLS = 8


def build(icon_dir, out_path):
    files = sorted(p for p in Path(icon_dir).glob("*.png"))
    if not files:
        print(f"no icons in {icon_dir}", file=sys.stderr)
        return {}, None
    rows = (len(files) + COLS - 1) // COLS
    sheet = Image.new("RGBA", (COLS * CELL, rows * CELL), (0, 0, 0, 0))
    cells = {}
    for i, f in enumerate(files):
        im = Image.open(f).convert("RGBA")
        if im.size != (CELL, CELL):
            print(f"  {f.name}: {im.size[0]}x{im.size[1]} (expected {CELL}x{CELL}) -- centered",
                  file=sys.stderr)
        col, row = i % COLS, i // COLS
        sheet.paste(im, (col * CELL + (CELL - im.width) // 2,
                         row * CELL + (CELL - im.height) // 2))
        cells[f.stem] = (col, row)
    sheet.save(out_path)
    return cells, sheet


def check(cells, config_dir):
    """Do the item configs agree with the sheet this just built?"""
    problems = 0
    for name, (col, row) in cells.items():
        p = Path(config_dir) / f"{name}.json"
        if not p.exists():
            print(f"  {name}: no config/items/{name}.json for this icon")
            problems += 1
            continue
        doc = json.loads(p.read_text(encoding="utf-8"))
        authored = doc.get("icon_cell")
        if authored != [col, row]:
            print(f"  {name}: config says {authored}, sheet has [{col}, {row}]")
            problems += 1
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--icons", default="assets/sprites/items")
    ap.add_argument("--out", default="assets/tilesets/items.png")
    ap.add_argument("--configs", default="config/items")
    ap.add_argument("--check", action="store_true",
                    help="also verify every item config's icon_cell matches")
    args = ap.parse_args()

    cells, sheet = build(args.icons, args.out)
    if not sheet:
        return 1
    print(f"{len(cells)} icons -> {args.out} ({sheet.width}x{sheet.height})")
    for name, (col, row) in sorted(cells.items()):
        print(f"  {name:20} icon_cell [{col}, {row}]")
    if args.check:
        problems = check(cells, args.configs)
        print("configs agree with the sheet" if problems == 0
              else f"{problems} config(s) disagree -- update their icon_cell")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
