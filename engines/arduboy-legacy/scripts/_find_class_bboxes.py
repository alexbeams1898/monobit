"""Scan all 9 player class portrait bboxes in art/maincharevo.png and
find the actual lit-pixel bounds for each. Surfaces aspect-ratio
mismatches between source bbox and declared output size."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PIL import Image
import numpy as np

# (ident, current_bbox, output_w, output_h)
TARGETS = [
    ("player_penitent_lvl1_data", (145, 125, 280, 295), 26, 30),
    ("player_penitent_lvl2_data", None, None, None),  # tbd from toml
    ("player_penitent_lvl3_data", None, None, None),
    ("player_wretched_lvl1_data", (130, 438, 360, 597), 24, 28),
    ("player_wretched_lvl2_data", None, None, None),
    ("player_wretched_lvl3_data", None, None, None),
    ("player_heretic_lvl1_data",  (418, 721, 584, 958), 32, 40),
    ("player_heretic_lvl2_data",  None, None, None),
    ("player_heretic_lvl3_data",  None, None, None),
]

SOURCE = Path("art/maincharevo.png")


def main():
    # Pull current values from the toml so we don't hand-maintain the list.
    import re
    text = (Path("games/rpg/sprites.toml").read_text())
    blocks = re.split(r'\n(?=\[\[sprites\]\])', text)
    by_ident = {}
    for b in blocks:
        m = re.search(r'ident\s*=\s*"([^"]+)"', b)
        if not m:
            continue
        ident = m.group(1)
        wm = re.search(r'^width\s*=\s*(\d+)', b, re.M)
        hm = re.search(r'^height\s*=\s*(\d+)', b, re.M)
        bm = re.search(r'bbox\s*=\s*\{\s*x0\s*=\s*(\d+),\s*y0\s*=\s*(\d+),\s*x1\s*=\s*(\d+),\s*y1\s*=\s*(\d+)', b)
        if wm and hm and bm:
            by_ident[ident] = (
                (int(bm.group(1)), int(bm.group(2)), int(bm.group(3)), int(bm.group(4))),
                int(wm.group(1)),
                int(hm.group(1)),
            )

    img = Image.open(SOURCE).convert("L")
    arr = np.array(img)

    print(f"{'ident':<32} {'bbox WxH':>10} {'lit WxH':>10} {'aspect':>6} {'tgt':>6} {'mismatch':>10}")
    for ident, _, _, _ in TARGETS:
        info = by_ident.get(ident)
        if not info:
            print(f"{ident:<32} (not found in toml)")
            continue
        bbox, ow, oh = info
        x0, y0, x1, y1 = bbox
        crop = arr[y0:y1, x0:x1]
        lit = crop > 80
        if not lit.any():
            print(f"{ident:<32} no lit pixels")
            continue
        ys, xs = np.where(lit)
        lw, lh = xs.max() - xs.min() + 1, ys.max() - ys.min() + 1
        bbox_w, bbox_h = x1 - x0, y1 - y0
        a_lit = lw / lh
        a_tgt = ow / oh
        miss = (a_lit - a_tgt) / a_tgt * 100
        print(f"{ident:<32} {bbox_w:>4}x{bbox_h:<4} {lw:>4}x{lh:<4} "
              f"{a_lit:>6.3f} {a_tgt:>6.3f} {miss:>9.1f}%")


if __name__ == "__main__":
    main()
