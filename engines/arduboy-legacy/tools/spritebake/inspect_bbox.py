#!/usr/bin/env python3
"""Bbox inspector — for sprites that use `bbox = {x0, y0, x1, y1}` in the
manifest, render a contact sheet showing each figure's crop PLUS padded
context, with the bbox edges drawn in red.

The padded context (default 30 px on each side) lets you see what's just
outside the bbox — labels that were almost included, parts of the figure
that got clipped, neighboring sprites bleeding in. The red rectangle is
the actual crop the bake will use.

Usage:
    python -m tools.spritebake inspect-bbox \
        -m games/rpg/sprites.toml \
        -f player_   # filter to idents starting with this prefix
        [-o build/spritebake/bbox_inspect.png]
        [--padding 40]
"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from . import config


def render_bbox_inspect(
    manifest: config.Manifest,
    *,
    ident_prefix: str | None = None,
    padding: int = 30,
    out_path: Path | None = None,
) -> Path:
    sprites = [s for s in manifest.sprites if s.bbox is not None]
    if ident_prefix:
        sprites = [s for s in sprites if s.ident.startswith(ident_prefix)]
    if not sprites:
        raise SystemExit(
            f"no sprites with bbox{f' matching prefix {ident_prefix!r}' if ident_prefix else ''}"
        )

    # Compute padded crop for each. Group by source image so we share
    # the same source dimensions for clamping.
    cells: list[tuple[config.SpriteSpec, Image.Image]] = []
    for spec in sprites:
        img = Image.open(spec.source).convert("RGB")
        W, H = img.size
        b = spec.bbox
        # Clamp padded region to source bounds.
        px0 = max(0, b.x0 - padding)
        py0 = max(0, b.y0 - padding)
        px1 = min(W, b.x1 + padding)
        py1 = min(H, b.y1 + padding)
        crop = img.crop((px0, py0, px1, py1))
        # Where the bbox edges sit within the cropped image.
        edge_x0 = b.x0 - px0
        edge_y0 = b.y0 - py0
        edge_x1 = b.x1 - px0
        edge_y1 = b.y1 - py0
        draw = ImageDraw.Draw(crop)
        # Red rectangle at the bbox edges. 2-px wide for visibility.
        draw.rectangle(
            [(edge_x0, edge_y0), (edge_x1 - 1, edge_y1 - 1)],
            outline=(255, 0, 0),
            width=2,
        )
        cells.append((spec, crop))

    # Layout: 3 columns × N rows. Each cell shows ident + bbox-size label
    # below the image.
    n = len(cells)
    cols = 3 if n >= 3 else n
    rows = (n + cols - 1) // cols
    cell_w = max(c.size[0] for _, c in cells)
    cell_h = max(c.size[1] for _, c in cells)
    label_h = 30
    pad = 8
    sheet_w = cols * (cell_w + pad) + pad
    sheet_h = rows * (cell_h + label_h + pad) + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)

    try:
        # Try a small bitmap font; fall back to default if unavailable.
        font = ImageFont.truetype("consola.ttf", 14)
    except (OSError, IOError):
        font = ImageFont.load_default()

    for i, (spec, img) in enumerate(cells):
        c = i % cols
        r = i // cols
        x = pad + c * (cell_w + pad) + (cell_w - img.size[0]) // 2
        y = pad + r * (cell_h + label_h + pad)
        sheet.paste(img, (x, y))
        bw = spec.bbox.x1 - spec.bbox.x0
        bh = spec.bbox.y1 - spec.bbox.y0
        label1 = spec.ident
        label2 = f"bbox=({spec.bbox.x0},{spec.bbox.y0},{spec.bbox.x1},{spec.bbox.y1})  {bw}x{bh}"
        draw.text((pad + c * (cell_w + pad), y + cell_h + 2),
                  label1, fill=(220, 220, 220), font=font)
        draw.text((pad + c * (cell_w + pad), y + cell_h + 16),
                  label2, fill=(160, 160, 160), font=font)

    if out_path is None:
        out_path = Path("build/spritebake/bbox_inspect.png")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out_path)
    return out_path
