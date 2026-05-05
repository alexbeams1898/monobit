"""For each of the 9 player class portraits: re-tighten the bbox to the
lit-pixel bounds (with 2-px padding) and propose new output dimensions
that match the figure's natural aspect ratio.

Strategy: keep heights anchored to the existing declared height (so the
class height triangle is preserved — Heretic tallest, Wretched shortest)
and derive width from the source aspect. Then pad the bbox to match the
output aspect exactly so the pipeline doesn't squash anything.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "art" / "maincharevo.png"
TOML = REPO / "games" / "rpg" / "sprites.toml"

CLASS_IDENTS = [
    "player_penitent_lvl1_data",
    "player_penitent_lvl2_data",
    "player_penitent_lvl3_data",
    "player_wretched_lvl1_data",
    "player_wretched_lvl2_data",
    "player_wretched_lvl3_data",
    "player_heretic_lvl1_data",
    "player_heretic_lvl2_data",
    "player_heretic_lvl3_data",
]

PAD = 2


def parse_toml():
    text = TOML.read_text()
    blocks = re.split(r'\n(?=\[\[sprites\]\])', text)
    out = {}
    for b in blocks:
        m = re.search(r'ident\s*=\s*"([^"]+)"', b)
        if not m:
            continue
        ident = m.group(1)
        wm = re.search(r'^width\s*=\s*(\d+)', b, re.M)
        hm = re.search(r'^height\s*=\s*(\d+)', b, re.M)
        bm = re.search(r'bbox\s*=\s*\{\s*x0\s*=\s*(\d+),\s*y0\s*=\s*(\d+),\s*x1\s*=\s*(\d+),\s*y1\s*=\s*(\d+)', b)
        if wm and hm and bm:
            out[ident] = {
                "bbox": (int(bm.group(1)), int(bm.group(2)),
                         int(bm.group(3)), int(bm.group(4))),
                "w": int(wm.group(1)),
                "h": int(hm.group(1)),
            }
    return out


def main():
    img = np.array(Image.open(SOURCE).convert("L"))
    src_h, src_w = img.shape
    info = parse_toml()

    print(f"{'ident':<32} {'cur bbox':>16} {'cur out':>8} -> "
          f"{'new bbox':>16} {'new out':>8} {'aspect':>7}")
    print("-" * 95)

    for ident in CLASS_IDENTS:
        d = info[ident]
        x0, y0, x1, y1 = d["bbox"]
        cur_w, cur_h = d["w"], d["h"]

        # Find tight lit-pixel bounds inside current bbox
        crop = img[y0:y1, x0:x1]
        lit = crop > 80
        ys, xs = np.where(lit)
        lx0, ly0 = xs.min(), ys.min()
        lx1, ly1 = xs.max() + 1, ys.max() + 1
        # Translate to absolute coords
        ax0 = x0 + lx0
        ay0 = y0 + ly0
        ax1 = x0 + lx1
        ay1 = y0 + ly1

        # Pad with PAD px (clamped to source bounds)
        px0 = max(0, ax0 - PAD)
        py0 = max(0, ay0 - PAD)
        px1 = min(src_w, ax1 + PAD)
        py1 = min(src_h, ay1 + PAD)

        # Source aspect (after padding)
        src_aw = px1 - px0
        src_ah = py1 - py0
        src_aspect = src_aw / src_ah

        # Anchor on existing declared HEIGHT and derive width from aspect
        new_h = cur_h
        new_w = int(round(new_h * src_aspect))

        # Pad the bbox vertically OR horizontally to make its aspect equal
        # the OUTPUT aspect exactly. Otherwise the pipeline still resizes
        # with a 1-2% mismatch, which over many pixels becomes a visible
        # 1-px shift.
        out_aspect = new_w / new_h
        if abs(src_aspect - out_aspect) < 0.001:
            final_x0, final_y0, final_x1, final_y1 = px0, py0, px1, py1
        elif src_aspect > out_aspect:
            # Source too wide for output → grow source HEIGHT
            target_h = int(round(src_aw / out_aspect))
            extra = target_h - src_ah
            half = extra // 2
            final_y0 = max(0, py0 - half)
            final_y1 = min(src_h, py1 + (extra - half))
            final_x0, final_x1 = px0, px1
        else:
            # Source too tall for output → grow source WIDTH
            target_w = int(round(src_ah * out_aspect))
            extra = target_w - src_aw
            half = extra // 2
            final_x0 = max(0, px0 - half)
            final_x1 = min(src_w, px1 + (extra - half))
            final_y0, final_y1 = py0, py1

        cur_str = f"{x1-x0}x{y1-y0}"
        new_bbox_str = f"{final_x1-final_x0}x{final_y1-final_y0}"
        print(f"{ident:<32} {cur_str:>16} {cur_w}x{cur_h:<5} -> "
              f"{new_bbox_str:>16} {new_w}x{new_h:<5} {src_aspect:>7.3f}")
        # Emit machine-readable for the patcher
        print(f"# PATCH {ident} bbox=({final_x0},{final_y0},{final_x1},{final_y1}) wh=({new_w},{new_h})")


if __name__ == "__main__":
    main()
