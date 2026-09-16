#!/usr/bin/env python3
"""Comment-density linter.

Per-file comment-to-line ratio across every active source tree. Headers and
implementation files are held to separate thresholds: a header carries a real
public-API doc burden, a .cpp mostly does not.

Both thresholds are ratchets. They start just under what the tree already
manages and come down as files are trimmed; the point is that the number can
only ever move one way.

Usage:
    python scripts/check_comment_density.py
    python scripts/check_comment_density.py --threshold 25 --header-threshold 55
    python scripts/check_comment_density.py --top 20
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# Trees are DERIVED, not listed. A hand-maintained list of directories silently
# stops covering a game the day one is added, and nothing anywhere says so.
TREE_PARENTS = ("engines", "games")

# Archived imports, held to the rules of the repo they came from.
ARCHIVED = ("arduboy-legacy", "rpg-arduboy")

EXCLUDES = ("build", "_deps", "third_party", "vendor", ".git")

LINE_COMMENT = re.compile(r"\s*//")
BLOCK_OPEN = re.compile(r"\s*/\*")

CPP_EXTENSIONS = (".cpp", ".cc", ".cxx")
HEADER_EXTENSIONS = (".h", ".hpp")

# Files this short have no meaningful ratio -- a 6-line header with a 2-line
# licence banner is not a density problem.
MIN_LINES = 20


def source_trees(repo_root: pathlib.Path) -> list[pathlib.Path]:
    trees = []
    for parent in TREE_PARENTS:
        base = repo_root / parent
        if not base.is_dir():
            continue
        for child in sorted(base.iterdir()):
            if child.is_dir() and child.name not in ARCHIVED:
                trees.append(child)
    return trees


def excluded(path: pathlib.Path) -> bool:
    return any(part in EXCLUDES for part in path.parts)


def count_lines(path: pathlib.Path) -> tuple[int, int]:
    """Return (comment_lines, total_lines). A line counts as comment if it
    opens with `//` or `/*`, or falls inside an open block comment. Blank
    lines count toward the total but not toward comments."""
    comment = 0
    total = 0
    in_block = False
    try:
        with path.open("r", encoding="utf-8", errors="replace") as f:
            for raw in f:
                total += 1
                stripped = raw.strip()
                if not stripped:
                    continue
                if in_block:
                    comment += 1
                    if "*/" in stripped:
                        in_block = False
                    continue
                if BLOCK_OPEN.match(raw):
                    comment += 1
                    if "*/" not in stripped:
                        in_block = True
                    continue
                if LINE_COMMENT.match(raw):
                    comment += 1
    except OSError as e:
        print(f"warn: could not read {path}: {e}", file=sys.stderr)
    return comment, total


def collect(repo_root: pathlib.Path) -> tuple[list, list]:
    cpp, headers = [], []
    for tree in source_trees(repo_root):
        for p in tree.rglob("*"):
            if not p.is_file() or excluded(p):
                continue
            if p.suffix in CPP_EXTENSIONS:
                bucket = cpp
            elif p.suffix in HEADER_EXTENSIONS:
                bucket = headers
            else:
                continue
            c, t = count_lines(p)
            if t < MIN_LINES:
                continue
            bucket.append((p.relative_to(repo_root), c, t, c / t * 100.0))
    cpp.sort(key=lambda r: r[3], reverse=True)
    headers.sort(key=lambda r: r[3], reverse=True)
    return cpp, headers


def report(label: str, rows: list, threshold: float, top: int) -> list:
    if not rows:
        return []
    total_c = sum(r[1] for r in rows)
    total_t = sum(r[2] for r in rows)
    pct = total_c / total_t * 100.0 if total_t else 0.0
    print(f"[comment-density] {label}: {len(rows)} files, {pct:.1f}% overall (limit {threshold:g}%)")
    for path, c, t, p in rows[:top]:
        flag = "  FAIL" if p > threshold else ""
        print(f"    {p:5.1f}%  {c:5d}/{t:5d}  {path}{flag}")
    return [r for r in rows if r[3] > threshold]


def main() -> int:
    ap = argparse.ArgumentParser(description="Per-file comment-density linter.")
    ap.add_argument("--threshold", type=float, default=30.0,
                    help="Max comment %% for .cpp files (default: 30).")
    ap.add_argument("--header-threshold", type=float, default=70.0,
                    help="Max comment %% for .h/.hpp files (default: 70).")
    ap.add_argument("--top", type=int, default=10, help="Offenders to print per bucket.")
    ap.add_argument("--repo-root", type=pathlib.Path, default=None)
    args = ap.parse_args()

    repo_root = (args.repo_root or pathlib.Path(__file__).resolve().parents[1]).resolve()

    cpp, headers = collect(repo_root)
    if not cpp and not headers:
        print(f"no source files found under {repo_root}", file=sys.stderr)
        return 1

    over = report("implementation", cpp, args.threshold, args.top)
    print()
    over += report("headers", headers, args.header_threshold, args.top)

    if over:
        print(f"\n[comment-density] {len(over)} file(s) over threshold:", file=sys.stderr)
        for path, _c, _t, pct in over:
            print(f"    {pct:5.1f}%  {path}", file=sys.stderr)
        print("\nComments state a live constraint the code cannot. Trim the rest.",
              file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
