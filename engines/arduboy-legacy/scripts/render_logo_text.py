#!/usr/bin/env python3
"""Render text using a TTF font into a 1-bit PNG, sized for the engine's
sprite pipeline. Outputs art/<name>.png; convert with png_to_sprite.py.

Usage:
    python scripts/render_logo_text.py HELL --font art/fonts/GothicPixels.ttf --size 16 --name logo_hell
"""

from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("text")
    ap.add_argument("--font", required=True)
    ap.add_argument("--size", type=int, required=True, help="font pixel size")
    ap.add_argument("--name", required=True, help="output filename (under art/, no ext)")
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--pad", type=int, default=0)
    ap.add_argument("--supersample", type=int, default=1,
                    help="render at NxNsize then downsample with box filter "
                         "(2-4 makes thresholded serif fonts cleaner; 1 = direct render)")
    args = ap.parse_args()

    ss = max(1, args.supersample)
    font = ImageFont.truetype(args.font, args.size * ss)
    bbox = font.getbbox(args.text)
    text_w = bbox[2] - bbox[0]
    text_h = bbox[3] - bbox[1]

    canvas_w = text_w + args.pad * 2 * ss
    canvas_h = text_h + 2 * ss

    img = Image.new("L", (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((args.pad * ss - bbox[0], -bbox[1]), args.text, fill=255, font=font)

    if ss > 1:
        # Downsample with averaging — preserves connected strokes that would
        # break under direct-render-then-threshold.
        img = img.resize((canvas_w // ss, canvas_h // ss), Image.LANCZOS)

    img = img.point(lambda p: 255 if p >= args.threshold else 0)

    bbox2 = img.getbbox()
    if bbox2:
        img = img.crop(bbox2)

    out = Path("art") / f"{args.name}.png"
    out.parent.mkdir(exist_ok=True)
    img.save(out)
    print(f"wrote {out} ({img.size[0]}x{img.size[1]})")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
