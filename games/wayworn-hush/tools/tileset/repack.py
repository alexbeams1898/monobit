"""Repack third-party sprites onto a clean authoring grid.

Some packs ship their sheet packed edge-to-edge at arbitrary offsets, which an
LDtk tileset (a strict grid) can't address. This lays every sprite out again
snapped to the authoring grid (default 16px), sorted tall-to-short so rows pack
tight. Output: a grid-aligned authoring source PNG plus its x2 nearest render
atlas twin (the engine convention -- see docs/design/MAP-PIPELINE.md).

Usage:
    python repack.py <input> <SourceName.png> <atlas.png> [--grid 16]

<input> is either a DIRECTORY of per-sprite PNGs (a pack's slice exports --
preferred: the pack itself says where each sprite begins and ends) or a packed
sheet PNG, from which sprites are found as connected opaque regions. The sheet
path only works when sprites have transparent gaps between them; a pack whose
art touches edge-to-edge needs the directory form.

The OUTPUT is the committed artifact: `assets/tilesets/source/<Name>.png` is
what LDtk paints from, and its x2 twin is what the game renders. A vendor pack's
raw slice exports are a local input, not repo content -- re-download the pack if
a sheet ever needs regenerating (see CREDITS.md for its source).
"""

import argparse
import sys
from collections import deque
from pathlib import Path

from PIL import Image


def sprite_boxes(im):
    """Bounding boxes of connected opaque regions (8-connectivity)."""
    w, h = im.size
    alpha = im.getchannel("A").load()
    seen = [[False] * h for _ in range(w)]
    boxes = []
    for sx in range(w):
        for sy in range(h):
            if seen[sx][sy] or alpha[sx, sy] == 0:
                continue
            # BFS this region.
            x0, y0, x1, y1 = sx, sy, sx, sy
            q = deque([(sx, sy)])
            seen[sx][sy] = True
            while q:
                x, y = q.popleft()
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x), max(y1, y)
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < w and 0 <= ny < h and not seen[nx][ny] \
                                and alpha[nx, ny] != 0:
                            seen[nx][ny] = True
                            q.append((nx, ny))
            boxes.append([x0, y0, x1 + 1, y1 + 1])
    return merge_boxes(boxes)


def merge_boxes(boxes):
    """Merge boxes that overlap or abut within 1px, to fixpoint."""
    merged = True
    while merged:
        merged = False
        out = []
        for b in boxes:
            for o in out:
                if b[0] <= o[2] + 1 and o[0] <= b[2] + 1 and \
                        b[1] <= o[3] + 1 and o[1] <= b[3] + 1:
                    o[0], o[1] = min(o[0], b[0]), min(o[1], b[1])
                    o[2], o[3] = max(o[2], b[2]), max(o[3], b[3])
                    merged = True
                    break
            else:
                out.append(list(b))
        boxes = out
    return boxes


def repack(sprites, grid, sheet_cols):
    """Lay sprite images out row by row on the grid, tall-to-short."""
    cells = lambda px: (px + grid - 1) // grid
    order = sorted(range(len(sprites)),
                   key=lambda i: (-sprites[i].height, -sprites[i].width))
    sheet_w = sheet_cols * grid
    placements, cx, cy, row_h = [], 0, 0, 0
    for i in order:
        s = sprites[i]
        cw, ch = cells(s.width), cells(s.height)
        if cx + cw > sheet_cols:
            cx, cy = 0, cy + row_h
            row_h = 0
        placements.append((s, cx * grid, cy * grid))
        cx += cw
        row_h = max(row_h, ch)
    sheet_h = (cy + row_h) * grid
    out = Image.new("RGBA", (sheet_w, sheet_h), (0, 0, 0, 0))
    # Centered horizontally, BOTTOM-aligned within the sprite's cell block --
    # top-down props rest on their cell's floor line, so a short item painted
    # above another sits ON it instead of floating.
    spots = []
    for s, x, y in placements:
        pad_x = (cells(s.width) * grid - s.width) // 2
        pad_y = cells(s.height) * grid - s.height
        out.paste(s, (x + pad_x, y + pad_y))
        spots.append((x // grid, y // grid, cells(s.width), cells(s.height)))
    return out, spots


def load_sprites(path):
    """The input's sprites: every PNG in a directory, or a sheet's regions."""
    p = Path(path)
    if p.is_dir():
        # Sort by filename (numeric-aware) so reruns produce the same sheet.
        def key(f):
            digits = "".join(c for c in f.stem if c.isdigit())
            return (int(digits) if digits else 0, f.stem)
        files = sorted(p.glob("*.png"), key=key)
        sprites = [Image.open(f).convert("RGBA") for f in files]
        # Trim each to its opaque bounds -- slice exports often carry margins.
        return [s.crop(s.getbbox()) for s in sprites if s.getbbox()]
    im = Image.open(p).convert("RGBA")
    return [im.crop(tuple(b)) for b in sprite_boxes(im)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", help="directory of sprite PNGs, or a packed sheet")
    ap.add_argument("source_out")
    ap.add_argument("atlas_out")
    ap.add_argument("--grid", type=int, default=16)
    ap.add_argument("--cols", type=int, default=40,
                    help="output sheet width in grid cells")
    ap.add_argument("--contact", default=None,
                    help="also write an HTML contact sheet (each sprite enlarged, "
                         "labeled with its grid cell + name) for eyeballing")
    ap.add_argument("--labels", default=None,
                    help="JSON sidecar of {\"cx,cy\": name} identifications. Read "
                         "if present (names show on the contact sheet); missing "
                         "entries are written back as empty strings to fill in.")
    args = ap.parse_args()

    sprites = load_sprites(args.input)
    out, spots = repack(sprites, args.grid, args.cols)
    out.save(args.source_out)
    out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(args.atlas_out)
    labels = {}
    if args.labels:
        labels = sync_labels(args.labels, spots)
    if args.contact:
        write_contact_sheet(args.contact, out, args.grid, spots, labels)
    print(f"{len(sprites)} sprites -> {args.source_out} {out.size} "
          f"+ x2 atlas {args.atlas_out}")
    return 0


def sync_labels(path, spots):
    """Load the labels sidecar and make sure every sprite has an entry (empty
    string = not yet identified), preserving names already filled in."""
    import json
    p = Path(path)
    labels = json.loads(p.read_text(encoding="utf-8")) if p.exists() else {}
    for cx, cy, _, _ in spots:
        labels.setdefault(f"{cx},{cy}", "")
    p.write_text(json.dumps(labels, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return labels


def write_contact_sheet(path, sheet, grid, spots, labels):
    """An HTML page of every SPRITE, enlarged and captioned with its name (from
    the labels sidecar) and grid cell -- the pack ships no names, so eyes are
    the metadata and the sidecar is where they land."""
    import base64
    import io

    def data_uri(im):
        buf = io.BytesIO()
        im.save(buf, "PNG")
        return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()

    rows = []
    for cx, cy, cw, ch in sorted(spots, key=lambda s: (s[1], s[0])):
        crop = sheet.crop((cx * grid, cy * grid, (cx + cw) * grid, (cy + ch) * grid))
        big = crop.resize((crop.width * 4, crop.height * 4), Image.NEAREST)
        name = labels.get(f"{cx},{cy}", "")
        rows.append(f'<figure><img src="{data_uri(big)}">'
                    f"<figcaption><b>{name or '?'}</b><br>{cx},{cy}</figcaption></figure>")
    html = ("<meta charset='utf-8'><style>body{background:#23262a;color:#d8dad6;"
            "font:12px sans-serif}figure{display:inline-block;margin:4px;"
            "text-align:center;vertical-align:top}img{image-rendering:pixelated;"
            "border:1px solid #444}</style>" + "\n".join(rows))
    with open(path, "w", encoding="utf-8") as f:
        f.write(html)


if __name__ == "__main__":
    sys.exit(main())
