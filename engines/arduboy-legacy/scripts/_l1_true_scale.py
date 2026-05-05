"""Show the three L1 portraits at their true relative pixel sizes.

The standard spritebake preview centers every sprite inside a max-sized
cell, which masks size differences. This script bakes each sprite and
composites them onto a canvas where each occupies exactly its native
WxH (scaled uniformly), bottom-aligned to a common baseline so height
differences are visible at a glance.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PIL import Image, ImageDraw, ImageFont

from tools.spritebake import config
from tools.spritebake.__main__ import _load_sprite_source, REPO_ROOT

MANIFEST_PATH = REPO_ROOT / "games" / "rpg" / "sprites.toml"
IDENTS = [
    "player_penitent_lvl1_data",
    "player_wretched_lvl1_data",
    "player_heretic_lvl1_data",
]
SCALE = 6
PAD = 12
BASELINE_PAD = 16
LABEL_H = 28


def bake_one(m, ident: str):
    spec = next(s for s in m.sprites if s.ident == ident)
    src = _load_sprite_source(spec)
    pipeline = spec.resolve_pipeline(m.pipelines)
    result = pipeline.run(src)
    return spec, result


def to_image(sprite, invert_for_white_bg: bool = True) -> Image.Image:
    arr = sprite.pixels
    h, w = arr.shape
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if arr[y, x] else 0
    if invert_for_white_bg:
        img = Image.eval(img, lambda v: 255 - v)
    return img


def main() -> None:
    m = config.load_manifest(MANIFEST_PATH, repo_root=REPO_ROOT)
    baked = [bake_one(m, ident) for ident in IDENTS]

    max_h = max(spec.height for spec, _ in baked)
    total_w = PAD + sum(spec.width * SCALE + PAD for spec, _ in baked)
    total_h = LABEL_H + max_h * SCALE + BASELINE_PAD * 2

    canvas = Image.new("L", (total_w, total_h), 255)
    draw = ImageDraw.Draw(canvas)
    try:
        font = ImageFont.truetype("arial.ttf", 12)
    except OSError:
        font = ImageFont.load_default()

    baseline_y = LABEL_H + max_h * SCALE + BASELINE_PAD
    draw.line([(0, baseline_y), (total_w, baseline_y)], fill=200, width=1)

    x = PAD
    for spec, sprite in baked:
        img = to_image(sprite, invert_for_white_bg=True)
        scaled = img.resize((spec.width * SCALE, spec.height * SCALE), Image.NEAREST)
        y = baseline_y - scaled.height
        canvas.paste(scaled, (x, y))
        label = spec.ident.replace("player_", "").replace("_lvl1_data", "")
        size_label = f"{spec.width}x{spec.height}"
        draw.text((x, 4), label, fill=0, font=font)
        draw.text((x, baseline_y + 4), size_label, fill=0, font=font)
        x += spec.width * SCALE + PAD

    out = REPO_ROOT / "art" / "_l1_true_scale.png"
    canvas.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
