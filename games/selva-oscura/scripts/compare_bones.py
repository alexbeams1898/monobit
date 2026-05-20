"""
compare_bones.py - Diff two bone-position CSVs frame-by-frame.

Both files must have the same schema:
    time_s, joint_name, x, y, z

The CSVs come from:
  * In-game F1 panel "Animation Debug" → "Record CSV"   →
        debug_bones_game_<clip>.csv  (lands next to the exe in build/bin/)
  * Browser previewer → "Export CSV" button             →
        debug_bones_browser_<clip>.csv  (downloads via the browser)

Usage:
    python compare_bones.py game.csv browser.csv [--threshold 0.01] [--joints HIPS,SPINE]

The script aligns the two by `time_s` (rounded to 4 decimal places to
avoid float-print drift). For every joint × frame pair, it computes the
Euclidean distance between game-side and browser-side world position.
Joints whose maximum-over-time divergence exceeds `--threshold` (in
meters) are reported.

Default threshold is 1cm (0.01m). Anything above that means the runtime
is noticeably deviating from the reference implementation.
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path


def normalize_joint_name(name: str) -> str:
    """Drop the ':' that ozz keeps but three.js's GLTFLoader strips.
    'mixamorig:Hips' (game-side) and 'mixamorigHips' (browser-side) both
    normalize to 'mixamorigHips'.
    """
    return name.replace(":", "")


def load_csv(path: Path) -> dict:
    """Returns { joint_name: { time_s_str: (x,y,z) } }, joint names normalized."""
    out: dict[str, dict[str, tuple[float, float, float]]] = defaultdict(dict)
    with path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                t_key = f"{float(row['time_s']):.4f}"
                x = float(row["x"])
                y = float(row["y"])
                z = float(row["z"])
                out[normalize_joint_name(row["joint_name"])][t_key] = (x, y, z)
            except (KeyError, ValueError) as ex:
                print(f"warn: malformed row {row}: {ex}", file=sys.stderr)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path, help="game-side CSV")
    parser.add_argument("browser", type=Path, help="browser-side CSV")
    parser.add_argument("--threshold", type=float, default=0.01,
                        help="report joints diverging by more than this (meters, default 0.01)")
    parser.add_argument("--joints", type=str, default="",
                        help="comma-separated substrings to filter (case-insensitive)")
    parser.add_argument("--top", type=int, default=20,
                        help="show this many top-divergent joints (default 20)")
    args = parser.parse_args()

    game = load_csv(args.game)
    browser = load_csv(args.browser)

    print(f"# game:     {args.game.name}  joints={len(game)} frames={len(next(iter(game.values()))) if game else 0}")
    print(f"# browser:  {args.browser.name}  joints={len(browser)} frames={len(next(iter(browser.values()))) if browser else 0}")

    filters = [s.lower() for s in args.joints.split(",") if s.strip()]
    def matches_filter(name: str) -> bool:
        if not filters:
            return True
        return any(f in name.lower() for f in filters)

    # Per-joint stats.
    rows = []
    common_joints = sorted(set(game.keys()) & set(browser.keys()))
    only_in_game = sorted(set(game.keys()) - set(browser.keys()))
    only_in_browser = sorted(set(browser.keys()) - set(game.keys()))

    if only_in_game:
        print(f"# joints only in game:    {only_in_game}")
    if only_in_browser:
        print(f"# joints only in browser: {only_in_browser}")

    for joint in common_joints:
        if not matches_filter(joint):
            continue
        g = game[joint]
        b = browser[joint]
        common_t = sorted(set(g.keys()) & set(b.keys()), key=lambda s: float(s))
        if not common_t:
            continue
        max_dev = 0.0
        max_t = ""
        sum_dev = 0.0
        for t in common_t:
            gx, gy, gz = g[t]
            bx, by, bz = b[t]
            d = math.sqrt((gx - bx) ** 2 + (gy - by) ** 2 + (gz - bz) ** 2)
            sum_dev += d
            if d > max_dev:
                max_dev = d
                max_t = t
        avg = sum_dev / len(common_t)
        rows.append((joint, max_dev, max_t, avg, len(common_t)))

    rows.sort(key=lambda r: r[1], reverse=True)

    flagged = [r for r in rows if r[1] > args.threshold]
    print()
    print(f"# joints exceeding threshold {args.threshold}m: {len(flagged)} / {len(rows)} compared")
    print()
    print(f"{'joint':36}  {'max_dev_m':>9}  {'at_t_s':>8}  {'avg_dev_m':>10}  {'frames':>6}")
    print("-" * 80)
    show_count = min(args.top, len(rows)) if not flagged else min(args.top, len(flagged))
    for joint, max_dev, max_t, avg, n in rows[:show_count]:
        marker = "*" if max_dev > args.threshold else " "
        print(f"{marker} {joint:34}  {max_dev:9.4f}  {max_t:>8}  {avg:10.4f}  {n:>6}")

    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main())
