"""Scan the current Wretched L1 bbox region in art/maincharevo.png and
find the actual lit-pixel bounds. We use this to retune sprites.toml's
bbox so the figure fills its 24x28 canvas (feet at bottom, head at top)
rather than floating in empty pixels.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PIL import Image
import numpy as np

SOURCE = Path("art/maincharevo.png")
# Current bbox in sprites.toml for player_wretched_lvl1_data
BBOX = (130, 438, 360, 597)  # x0, y0, x1, y1


def main():
    img = Image.open(SOURCE).convert("L")
    arr = np.array(img)
    print(f"source size: {arr.shape[1]}x{arr.shape[0]}")
    x0, y0, x1, y1 = BBOX
    crop = arr[y0:y1, x0:x1]
    print(f"current bbox crop size: {crop.shape[1]}x{crop.shape[0]}")
    # Source is white-on-black (mostly black bg with white figure pixels).
    # "Lit" = bright pixel.
    lit = crop > 80
    if not lit.any():
        print("no lit pixels in current bbox!")
        return
    ys, xs = np.where(lit)
    print(f"lit pixel y range: {ys.min()}..{ys.max()} (height {ys.max() - ys.min() + 1})")
    print(f"lit pixel x range: {xs.min()}..{xs.max()} (width  {xs.max() - xs.min() + 1})")
    # Translate to absolute source coords
    abs_x0 = x0 + xs.min()
    abs_y0 = y0 + ys.min()
    abs_x1 = x0 + xs.max() + 1
    abs_y1 = y0 + ys.max() + 1
    print()
    print(f"tight absolute bbox: x0={abs_x0} y0={abs_y0} x1={abs_x1} y1={abs_y1}")
    print(f"tight w x h: {abs_x1 - abs_x0} x {abs_y1 - abs_y0}")
    print()
    # Suggest a small breathing margin (1-2 px) so threshold artifacts at
    # the very edge don't get clipped during the resize.
    pad = 2
    sug_x0 = max(0, abs_x0 - pad)
    sug_y0 = max(0, abs_y0 - pad)
    sug_x1 = min(arr.shape[1], abs_x1 + pad)
    sug_y1 = min(arr.shape[0], abs_y1 + pad)
    print(f"with {pad}-px breathing pad: x0={sug_x0} y0={sug_y0} x1={sug_x1} y1={sug_y1}")
    print(f"padded w x h: {sug_x1 - sug_x0} x {sug_y1 - sug_y0}")
    aspect_padded = (sug_x1 - sug_x0) / (sug_y1 - sug_y0)
    print(f"padded aspect (w/h): {aspect_padded:.3f}")
    # Target output 24x28: aspect 0.857. If padded aspect doesn't match,
    # the pipeline's resize will distort. Print the difference.
    target_aspect = 24 / 28
    print(f"target 24x28 aspect:  {target_aspect:.3f}")
    if abs(aspect_padded - target_aspect) > 0.05:
        # Adjust bbox to match target aspect by extending the SHORTER axis.
        # We want the lit pixels to remain centered in the bbox.
        ph = sug_y1 - sug_y0
        pw = sug_x1 - sug_x0
        if aspect_padded > target_aspect:
            # too wide → grow height
            new_h = int(round(pw / target_aspect))
            extra = new_h - ph
            half = extra // 2
            new_y0 = max(0, sug_y0 - half)
            new_y1 = min(arr.shape[0], sug_y1 + (extra - half))
            print(f"aspect-corrected (grow height): "
                  f"x0={sug_x0} y0={new_y0} x1={sug_x1} y1={new_y1} "
                  f"({sug_x1 - sug_x0} x {new_y1 - new_y0})")
        else:
            # too tall → grow width
            new_w = int(round(ph * target_aspect))
            extra = new_w - pw
            half = extra // 2
            new_x0 = max(0, sug_x0 - half)
            new_x1 = min(arr.shape[1], sug_x1 + (extra - half))
            print(f"aspect-corrected (grow width):  "
                  f"x0={new_x0} y0={sug_y0} x1={new_x1} y1={sug_y1} "
                  f"({new_x1 - new_x0} x {sug_y1 - sug_y0})")


if __name__ == "__main__":
    main()
