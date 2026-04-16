"""
Shrink a weapon icon within its 32x32 canvas.

Useful when an icon pack has source art that's too large relative to the
character in-game (e.g. a pistol drawn at the same scale as a rifle).

Crops the icon to its non-transparent bounding box, scales it with
nearest-neighbor to `scale * source_size`, then pastes it centered into a
fresh 32x32 transparent canvas. Overwrites the input file in place.

Because the source file in the repo is what gets shipped (there's no
separate "masters" folder), this operation is destructive. Use git to
revert if you don't like the result.

Usage:
  python scripts/resize_weapon_icon.py assets/sprites/icons/colt_45.png --scale 0.65
  python scripts/resize_weapon_icon.py assets/sprites/icons/colt_45.png --scale 0.65 --out preview.png
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError as e:
    print(f"ERROR: Missing dependency: {e}")
    print("Install with: pip install Pillow")
    sys.exit(1)

CANVAS_SIZE = 32


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("icon", help="Path to the icon PNG (absolute or relative to cwd)")
    parser.add_argument(
        "--scale",
        type=float,
        required=True,
        help="Scale factor for the non-transparent bounding box (e.g. 0.65).",
    )
    parser.add_argument(
        "--out",
        default=None,
        help="Output path. Default: overwrite the input.",
    )
    args = parser.parse_args()

    icon_path = args.icon
    if not os.path.isabs(icon_path):
        icon_path = os.path.abspath(icon_path)
    if not os.path.exists(icon_path):
        print(f"ERROR: icon not found: {icon_path}")
        sys.exit(1)

    if args.scale <= 0 or args.scale > 1:
        print(f"ERROR: scale must be in (0, 1]; got {args.scale}")
        sys.exit(1)

    src = Image.open(icon_path).convert("RGBA")
    if src.size != (CANVAS_SIZE, CANVAS_SIZE):
        print(f"WARNING: source is {src.size}, expected ({CANVAS_SIZE}, {CANVAS_SIZE})")

    # Crop to the non-transparent bounding box so the scale factor applies
    # to the visible art, not empty padding.
    bbox = src.getbbox()
    if bbox is None:
        print("ERROR: icon is fully transparent, nothing to scale")
        sys.exit(1)
    cropped = src.crop(bbox)
    cw, ch = cropped.size

    new_w = max(1, round(cw * args.scale))
    new_h = max(1, round(ch * args.scale))
    resized = cropped.resize((new_w, new_h), Image.NEAREST)

    canvas = Image.new("RGBA", (CANVAS_SIZE, CANVAS_SIZE), (0, 0, 0, 0))
    off_x = (CANVAS_SIZE - new_w) // 2
    off_y = (CANVAS_SIZE - new_h) // 2
    canvas.paste(resized, (off_x, off_y), resized)

    out_path = args.out if args.out else icon_path
    if not os.path.isabs(out_path):
        out_path = os.path.abspath(out_path)
    canvas.save(out_path)

    print(f"Source bbox: {bbox}  ({cw}x{ch})")
    print(f"Resized to:  {new_w}x{new_h}  (scale {args.scale})")
    print(f"Pasted at:   ({off_x}, {off_y}) in a {CANVAS_SIZE}x{CANVAS_SIZE} canvas")
    print(f"Saved ->     {out_path}")
    print()
    print("NOTE: icon's pixel layout changed. Re-run measure_weapon_grip.py on the")
    print("      corresponding weapon JSON so grip_x/grip_y point at the new grip pixel.")


if __name__ == "__main__":
    main()
