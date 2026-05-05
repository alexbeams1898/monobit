"""Bake all 9 player class portraits + 9 world variants and lay them out
at true scale so we can confirm the source art is good enough before
doing the sprite-ID integration work.

Output: art/_player_class_preview.png — 3x3 grid of portraits (rows =
classes, cols = levels), with the world variants stacked below in the
same grid.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from PIL import Image, ImageDraw, ImageFont

from tools.spritebake import config
from tools.spritebake.__main__ import _load_sprite_source, REPO_ROOT

MANIFEST_PATH = REPO_ROOT / "games" / "rpg" / "sprites.toml"

CLASSES = ["penitent", "wretched", "heretic"]
LEVELS = [1, 2, 3]

PORTRAIT_SCALE = 5
WORLD_SCALE = 8  # world sprites are ~12x16 — scale them up more so they're visible
PAD = 12
LABEL_H = 22


def bake_one(m, ident: str):
    spec = next((s for s in m.sprites if s.ident == ident), None)
    if spec is None:
        return None, None
    src = _load_sprite_source(spec)
    pipeline = spec.resolve_pipeline(m.pipelines)
    result = pipeline.run(src)
    return spec, result


def to_image(sprite, invert: bool = False) -> Image.Image:
    arr = sprite.pixels
    h, w = arr.shape
    img = Image.new("L", (w, h), 0)
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if arr[y, x] else 0
    if invert:
        img = Image.eval(img, lambda v: 255 - v)
    return img


def render_grid(m, kind: str, scale: int):
    """kind == 'portrait' uses player_<class>_lvl<L>_data;
    kind == 'world' uses player_<class>_lvl<L>_world_data.

    Penitent L1's WORLD cell is overridden to use pen_l1_idle_f0_data —
    the touched-up idle frame is the canonical in-world L1 sprite. The
    portrait cell still uses the 26x30 player_penitent_lvl1_data."""
    suffix = "_data" if kind == "portrait" else "_world_data"
    grid = []
    for cls in CLASSES:
        row = []
        for lvl in LEVELS:
            if kind == "world" and cls == "penitent" and lvl == 1:
                ident = "pen_l1_idle_f0_data"
            else:
                ident = f"player_{cls}_lvl{lvl}{suffix}"
            spec, sprite = bake_one(m, ident)
            row.append((ident, spec, sprite))
        grid.append(row)
    # find max cell w/h in pixels at native size
    max_w = max((spec.width for row in grid for _, spec, _ in row if spec), default=1)
    max_h = max((spec.height for row in grid for _, spec, _ in row if spec), default=1)
    cell_w = max_w * scale
    cell_h = max_h * scale + LABEL_H

    n_rows = len(CLASSES)
    n_cols = len(LEVELS)
    img_w = PAD + n_cols * (cell_w + PAD)
    img_h = LABEL_H + n_rows * (cell_h + PAD)

    out = Image.new("L", (img_w, img_h), 0)
    draw = ImageDraw.Draw(out)
    try:
        font = ImageFont.truetype("arial.ttf", 12)
    except OSError:
        font = ImageFont.load_default()

    # column headers
    for ci, lvl in enumerate(LEVELS):
        x = PAD + ci * (cell_w + PAD)
        draw.text((x + cell_w // 2 - 18, 4), f"L{lvl}", fill=255, font=font)

    # rows
    for ri, cls in enumerate(CLASSES):
        y_top = LABEL_H + ri * (cell_h + PAD)
        # row label
        draw.text((4, y_top + cell_h // 2 - 8), cls.upper()[:4], fill=255, font=font)
        for ci in range(n_cols):
            ident, spec, sprite = grid[ri][ci]
            x = PAD + ci * (cell_w + PAD)
            cell_baseline = y_top + max_h * scale
            if spec is None or sprite is None:
                draw.text((x + 4, y_top + 4), "MISSING", fill=128, font=font)
                continue
            img = to_image(sprite, invert=False)
            scaled = img.resize((spec.width * scale, spec.height * scale), Image.NEAREST)
            paste_x = x + (cell_w - scaled.width) // 2
            paste_y = cell_baseline - scaled.height
            out.paste(scaled, (paste_x, paste_y))
            # baseline ruler
            draw.line([(x, cell_baseline), (x + cell_w, cell_baseline)], fill=80)
            # size label
            draw.text((x + 4, cell_baseline + 4), f"{spec.width}x{spec.height}",
                      fill=200, font=font)
    return out


def main():
    m = config.load_manifest(MANIFEST_PATH, repo_root=REPO_ROOT)

    portraits = render_grid(m, "portrait", PORTRAIT_SCALE)
    worlds = render_grid(m, "world", WORLD_SCALE)

    # combine vertically with section labels
    spacer = 30
    title_h = 32
    total_w = max(portraits.width, worlds.width)
    total_h = title_h + portraits.height + spacer + title_h + worlds.height + 10

    canvas = Image.new("L", (total_w, total_h), 0)
    draw = ImageDraw.Draw(canvas)
    try:
        font_big = ImageFont.truetype("arial.ttf", 16)
    except OSError:
        font_big = ImageFont.load_default()

    draw.text((10, 8), "PORTRAITS (RECKONING-tier)", fill=255, font=font_big)
    canvas.paste(portraits, (0, title_h))
    y2 = title_h + portraits.height + spacer
    draw.text((10, y2 - 22), "WORLD (in-game token)", fill=255, font=font_big)
    canvas.paste(worlds, (0, y2))

    out = REPO_ROOT / "art" / "_player_class_preview.png"
    canvas.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
