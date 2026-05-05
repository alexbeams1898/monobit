"""
Auto-generate a hurtbox from any sprite's silhouette.

Reads the alpha channel of a single frame, finds contiguous vertical bands
of opaque pixels, and fits a circle to each band. Outputs the resulting
shapes either to stdout (--dry-run) or merged into a target JSON file under
a specified key.

Example uses:
  # Humanoid body sheet -> inherit-hurtbox for all LPC humanoids
  python scripts/gen_hurtbox_from_sprite.py \
      --sheet game/assets/sprites/lpc/assembled/body/body_master.png \
      --anim game/config/animations/lpc_humanoid.json

  # A standalone sprite -> print shapes without writing
  python scripts/gen_hurtbox_from_sprite.py \
      --sheet some_sprite.png --frame-size 32 --dry-run
"""

import argparse
import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow required. pip install Pillow")
    sys.exit(1)

DEFAULT_ALPHA_THRESHOLD = 32
# Minimum gap (opaque-row count) between clusters. Rows separated by this
# many empty rows split into distinct regions (e.g. head + torso on a full
# character composite).
DEFAULT_MIN_GAP = 2

# Anatomical region ratios for humanoid silhouettes. When --regions humanoid
# is passed, we split the silhouette vertically at these fractions regardless
# of alpha gaps (neck/waist are solid pixels, not empty). Values are fractions
# of the opaque y-range (ymin..ymax).
HUMANOID_HEAD_FRACTION = 0.25
HUMANOID_TORSO_FRACTION = 0.6  # bottom of torso -> top of legs


def frame_silhouette(image_path, frame_size, row, col, alpha_threshold):
    img = Image.open(image_path).convert("RGBA")
    left = col * frame_size
    top = row * frame_size
    cropped = img.crop((left, top, left + frame_size, top + frame_size))
    alpha = cropped.split()[-1]
    px = alpha.load()
    return {
        (x, y)
        for y in range(frame_size)
        for x in range(frame_size)
        if px[x, y] >= alpha_threshold
    }


def union_silhouettes(image_paths, frame_size, row, col, alpha_threshold):
    """Read each sprite's opaque pixels and union them into a single set."""
    combined = set()
    for path in image_paths:
        combined |= frame_silhouette(path, frame_size, row, col, alpha_threshold)
    return list(combined)


def cluster_by_vertical_gaps(pixels, min_gap):
    """Partition opaque pixels into vertical clusters separated by >= min_gap empty rows."""
    rows_with_pixels = sorted({y for _, y in pixels})
    if not rows_with_pixels:
        return []

    clusters = []
    current = [rows_with_pixels[0]]
    for y in rows_with_pixels[1:]:
        if y - current[-1] >= min_gap + 1:
            clusters.append(current)
            current = [y]
        else:
            current.append(y)
    clusters.append(current)

    # Collect pixels by cluster y-range.
    out = []
    for cluster in clusters:
        ymin, ymax = min(cluster), max(cluster)
        out.append([p for p in pixels if ymin <= p[1] <= ymax])
    return out


def cluster_humanoid(pixels):
    """Split the silhouette into head/torso/legs by fixed anatomical ratios.
    LPC humanoids share a common proportion so a fixed split is reliable."""
    ys = [y for _, y in pixels]
    if not ys:
        return []
    ymin = min(ys)
    ymax = max(ys)
    height = ymax - ymin + 1
    head_end = ymin + int(height * HUMANOID_HEAD_FRACTION)
    torso_end = ymin + int(height * HUMANOID_TORSO_FRACTION)

    head = [p for p in pixels if p[1] <= head_end]
    torso = [p for p in pixels if head_end < p[1] <= torso_end]
    legs = [p for p in pixels if p[1] > torso_end]
    return [c for c in (head, torso, legs) if c]


def fit_circle(pixels, shrink=0.85):
    if not pixels:
        return None
    xs = [p[0] for p in pixels]
    ys = [p[1] for p in pixels]
    cx = (min(xs) + max(xs)) / 2.0
    cy = (min(ys) + max(ys)) / 2.0
    r = max(((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 for x, y in pixels)
    return (cx, cy, r * shrink)


def default_label(index, total):
    """Heuristic: top region = head (if 3+), middle = torso, bottom = legs."""
    if total >= 3:
        if index == 0:
            return "head", 2.0
        if index == total - 1:
            return "legs", 0.8
        return "torso", 1.0
    if total == 2:
        return ("torso", 1.0) if index == 0 else ("legs", 0.8)
    return "body", 1.0


def build_hurtbox(pixels, frame_size, min_gap, shrink, regions, origin_y):
    """Fit shapes to the silhouette.
    `origin_y` is the sprite-local y coordinate that maps to world Transform.y.
    LPC humanoids render with the transform at the collider center, which sits
    lower than the frame center because the sprite is 64x64 but the collider
    is only 48 tall. Pass origin_y=37 to align hurtboxes with world positions.
    """
    origin_x = frame_size / 2.0
    if regions == "humanoid":
        clusters = cluster_humanoid(pixels)
    else:
        clusters = cluster_by_vertical_gaps(pixels, min_gap)
    shapes = []
    for i, cluster_px in enumerate(clusters):
        fit = fit_circle(cluster_px, shrink)
        if fit is None:
            continue
        cx, cy, r = fit
        label, dmg_mult = default_label(i, len(clusters))
        shapes.append(
            {
                "shape": "circle",
                "x": round(cx - origin_x, 2),
                "y": round(cy - origin_y, 2),
                "r": round(r, 2),
                "label": label,
                "dmg_mult": dmg_mult,
            }
        )
    return shapes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--sheet",
        action="append",
        required=True,
        help="path to a source spritesheet (pass multiple times to union layers)",
    )
    ap.add_argument(
        "--frame-size", type=int, default=64, help="frame size (default 64 for LPC)"
    )
    ap.add_argument("--row", type=int, default=0, help="spritesheet row to read (default 0 = idle)")
    ap.add_argument(
        "--col", type=int, default=0, help="spritesheet column (default 0 = south-facing first frame)"
    )
    ap.add_argument(
        "--alpha-threshold",
        type=int,
        default=DEFAULT_ALPHA_THRESHOLD,
        help="alpha cutoff; pixels above this count as body",
    )
    ap.add_argument(
        "--min-gap",
        type=int,
        default=DEFAULT_MIN_GAP,
        help="min empty-row gap to split regions",
    )
    ap.add_argument(
        "--shrink",
        type=float,
        default=0.85,
        help="radius shrink factor (1.0 = touch outer pixels, <1 = hug silhouette)",
    )
    ap.add_argument(
        "--regions",
        choices=["gaps", "humanoid"],
        default="humanoid",
        help="how to split: 'gaps' = alpha gaps, 'humanoid' = fixed head/torso/legs ratios",
    )
    ap.add_argument(
        "--origin-y",
        type=float,
        default=None,
        help="sprite pixel y that maps to world Transform.y (LPC humanoids: 37). Defaults to frame center.",
    )
    ap.add_argument(
        "--anim",
        default=None,
        help="if set, write shapes into this JSON under the --key key",
    )
    ap.add_argument("--key", default="hurtbox", help="top-level JSON key to write under")
    ap.add_argument(
        "--dry-run", action="store_true", help="print shapes without writing"
    )
    args = ap.parse_args()

    for path in args.sheet:
        if not os.path.exists(path):
            print(f"sheet not found: {path}")
            sys.exit(1)

    pixels = union_silhouettes(
        args.sheet, args.frame_size, args.row, args.col, args.alpha_threshold
    )
    if not pixels:
        print(f"no opaque pixels found (alpha >= {args.alpha_threshold})")
        sys.exit(1)

    origin_y = args.origin_y if args.origin_y is not None else args.frame_size / 2.0
    shapes = build_hurtbox(
        pixels, args.frame_size, args.min_gap, args.shrink, args.regions, origin_y
    )
    print(f"Generated {len(shapes)} shape(s):")
    for s in shapes:
        print(f"  {s}")

    if args.dry_run or args.anim is None:
        return

    if not os.path.exists(args.anim):
        print(f"target JSON not found: {args.anim}")
        sys.exit(1)
    with open(args.anim) as f:
        data = json.load(f)
    data[args.key] = shapes
    with open(args.anim, "w") as f:
        json.dump(data, f, indent=4)
    print(f"Wrote {args.key} to {args.anim}")


if __name__ == "__main__":
    main()
