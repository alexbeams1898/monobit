"""Patch sprites.toml in-place: for each of the 9 player class portraits
(and their world variants), set the bbox + width + height to values
that preserve natural source aspect ratio.

Strategy:
  - Re-tighten bbox to lit-pixel bounds (with PAD breathing margin).
  - Pad bbox to *exactly* match the chosen output aspect, so the
    pipeline doesn't squash anything.
  - Anchor the new height to the existing declared height.
  - Derive new width = round(new_h * source_aspect).
  - For world variants: anchor on world's existing height; compute width
    from the SAME source aspect.
"""
from __future__ import annotations

import re
from pathlib import Path
import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "art" / "maincharevo.png"
TOML = REPO / "games" / "rpg" / "sprites.toml"

CLASSES = ["penitent", "wretched", "heretic"]
LEVELS = [1, 2, 3]
PAD = 2


def lit_bounds(img: np.ndarray, bbox):
    x0, y0, x1, y1 = bbox
    crop = img[y0:y1, x0:x1]
    lit = crop > 80
    ys, xs = np.where(lit)
    return (x0 + xs.min(), y0 + ys.min(),
            x0 + xs.max() + 1, y0 + ys.max() + 1)


def adjust_bbox_to_aspect(img_shape, bbox, target_aspect):
    """Pad bbox to make its (w/h) exactly equal target_aspect."""
    src_h, src_w = img_shape
    x0, y0, x1, y1 = bbox
    bw = x1 - x0
    bh = y1 - y0
    cur = bw / bh
    if abs(cur - target_aspect) < 0.001:
        return bbox
    if cur > target_aspect:
        # too wide -> grow height
        target_h = int(round(bw / target_aspect))
        extra = target_h - bh
        half = extra // 2
        ny0 = max(0, y0 - half)
        ny1 = min(src_h, y1 + (extra - half))
        return (x0, ny0, x1, ny1)
    else:
        # too tall -> grow width
        target_w = int(round(bh * target_aspect))
        extra = target_w - bw
        half = extra // 2
        nx0 = max(0, x0 - half)
        nx1 = min(src_w, x1 + (extra - half))
        return (nx0, y0, nx1, y1)


def parse_blocks(text):
    """Return list of (start_idx, end_idx, ident, body) for each
    [[sprites]] block in the file."""
    out = []
    pattern = re.compile(r'\[\[sprites\]\]')
    matches = list(pattern.finditer(text))
    for i, m in enumerate(matches):
        start = m.start()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        body = text[start:end]
        im = re.search(r'ident\s*=\s*"([^"]+)"', body)
        if im:
            out.append((start, end, im.group(1), body))
    return out


def parse_block_fields(body):
    out = {}
    wm = re.search(r'^(width\s*=\s*)(\d+)', body, re.M)
    hm = re.search(r'^(height\s*=\s*)(\d+)', body, re.M)
    bm = re.search(r'(bbox\s*=\s*\{\s*x0\s*=\s*)(\d+)(,\s*y0\s*=\s*)(\d+)(,\s*x1\s*=\s*)(\d+)(,\s*y1\s*=\s*)(\d+)(\s*\})', body)
    return wm, hm, bm


def patch_block(body, new_w, new_h, new_bbox):
    wm, hm, bm = parse_block_fields(body)
    body = body[:wm.start(2)] + str(new_w) + body[wm.end(2):]
    # Re-search after modification
    hm = re.search(r'^(height\s*=\s*)(\d+)', body, re.M)
    body = body[:hm.start(2)] + str(new_h) + body[hm.end(2):]
    bm = re.search(r'(bbox\s*=\s*\{\s*x0\s*=\s*)(\d+)(,\s*y0\s*=\s*)(\d+)(,\s*x1\s*=\s*)(\d+)(,\s*y1\s*=\s*)(\d+)(\s*\})', body)
    x0, y0, x1, y1 = new_bbox
    new_bbox_str = f"bbox = {{ x0 = {x0}, y0 = {y0}, x1 = {x1}, y1 = {y1} }}"
    body = body[:bm.start()] + new_bbox_str + body[bm.end():]
    return body


def main():
    text = TOML.read_text()
    img = np.array(Image.open(SOURCE).convert("L"))

    blocks = parse_blocks(text)
    by_ident = {ident: (start, end, body) for start, end, ident, body in blocks}

    plan = []  # (ident, new_w, new_h, new_bbox, old_summary, new_summary)

    for cls in CLASSES:
        for lvl in LEVELS:
            portrait_id = f"player_{cls}_lvl{lvl}_data"
            world_id = f"player_{cls}_lvl{lvl}_world_data"

            _, _, p_body = by_ident[portrait_id]
            wm, hm, bm = parse_block_fields(p_body)
            old_p_w, old_p_h = int(wm.group(2)), int(hm.group(2))
            old_bbox = tuple(int(bm.group(g)) for g in (2, 4, 6, 8))

            # World variant may not exist (Penitent L1 was retired in favor
            # of pen_l1_idle_f0_data). Skip world-dim computation if absent.
            if world_id in by_ident:
                wm2, hm2, bm2 = parse_block_fields(by_ident[world_id][2])
                old_w_w, old_w_h = int(wm2.group(2)), int(hm2.group(2))
            else:
                old_w_w, old_w_h = None, None

            # Compute lit-pixel bounds inside old bbox
            lx0, ly0, lx1, ly1 = lit_bounds(img, old_bbox)
            # Pad
            src_h, src_w = img.shape
            px0 = max(0, lx0 - PAD)
            py0 = max(0, ly0 - PAD)
            px1 = min(src_w, lx1 + PAD)
            py1 = min(src_h, ly1 + PAD)
            src_aspect = (px1 - px0) / (py1 - py0)

            # Anchor on existing portrait HEIGHT, derive width
            new_p_h = old_p_h
            new_p_w = int(round(new_p_h * src_aspect))
            target_aspect = new_p_w / new_p_h

            # Pad bbox to match output aspect exactly
            new_bbox = adjust_bbox_to_aspect((src_h, src_w),
                                             (px0, py0, px1, py1),
                                             target_aspect)

            # World variant: same source aspect, anchored on world height
            if old_w_h is not None:
                new_w_h = old_w_h
                new_w_w = int(round(new_w_h * src_aspect))
            else:
                new_w_h = new_w_w = None

            plan.append({
                "portrait_id": portrait_id,
                "world_id": world_id,
                "new_p_w": new_p_w,
                "new_p_h": new_p_h,
                "new_w_w": new_w_w,
                "new_w_h": new_w_h,
                "new_bbox": new_bbox,
                "old": f"{old_p_w}x{old_p_h} bbox=({old_bbox[0]},{old_bbox[1]})-({old_bbox[2]},{old_bbox[3]})",
                "new": f"{new_p_w}x{new_p_h} bbox=({new_bbox[0]},{new_bbox[1]})-({new_bbox[2]},{new_bbox[3]})",
            })

    print("PATCH PLAN:")
    print(f"{'ident':<30} {'old':<55} -> {'new':<55}")
    for p in plan:
        print(f"  {p['portrait_id']:<30} {p['old']:<55} -> {p['new']}")
        print(f"  {p['world_id']:<30} {old_w_w}x{old_w_h} (kept bbox)        -> {p['new_w_w']}x{p['new_w_h']}  (kept bbox)")

    # Apply the patches in REVERSE position order so byte offsets don't shift
    # under our feet. Only patch portraits — world variants have hand-tuned
    # touchups at their existing dimensions and shouldn't be touched here.
    edits = []  # (start, end, new_body)
    for p in plan:
        ps, pe, pb = by_ident[p["portrait_id"]]
        new_pb = patch_block(pb, p["new_p_w"], p["new_p_h"], p["new_bbox"])
        edits.append((ps, pe, new_pb))

    # Apply in reverse order
    edits.sort(key=lambda e: -e[0])
    new_text = text
    for s, e, nb in edits:
        new_text = new_text[:s] + nb + new_text[e:]

    TOML.write_text(new_text)
    print()
    print(f"wrote {TOML}  ({len(plan) * 2} blocks updated)")


if __name__ == "__main__":
    main()
