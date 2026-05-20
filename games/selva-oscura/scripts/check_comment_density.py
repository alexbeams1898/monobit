#!/usr/bin/env python3
"""Comment-density linter.

Walks the active source trees (engine, prison-escape-game, selva-oscura)
and computes per-file comment-to-total-lines ratio. Fails if any file
exceeds the per-file threshold.

Rationale: see .claude/CLAUDE.md "Code comment policy". Today's worst
offenders sit at 25-28%; HitDetection.cpp at 7% is the target shape.
Initial CI threshold is set high (30%) so existing files don't break;
ratchet down as files get trimmed.

Usage:
    python games/selva-oscura/scripts/check_comment_density.py
    python games/selva-oscura/scripts/check_comment_density.py --threshold 25
    python games/selva-oscura/scripts/check_comment_density.py --top 20
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

# Subtrees walked. Archived trees (arduboy-legacy, rpg-arduboy) are
# excluded — held to the mono repo's rules, not extended.
ROOTS = ("engines/engine", "games/prison-escape-game", "games/selva-oscura")
EXCLUDES = ("build", "_deps", "third_party", "vendor", ".git")

LINE_COMMENT = re.compile(r"\s*//")
BLOCK_OPEN = re.compile(r"\s*/\*")


CPP_EXTENSIONS = (".cpp", ".cc", ".cxx")
HEADER_EXTENSIONS = (".h", ".hpp")


def is_source(path: pathlib.Path, include_headers: bool) -> bool:
    if path.suffix in CPP_EXTENSIONS:
        return True
    if include_headers and path.suffix in HEADER_EXTENSIONS:
        return True
    return False


def excluded(path: pathlib.Path) -> bool:
    return any(part in EXCLUDES for part in path.parts)


def count_lines(path: pathlib.Path) -> tuple[int, int]:
    """Return (comment_lines, total_lines). A line is a "comment line" if
    it begins with `//`, `/*`, `*`, or is otherwise inside an open block
    comment. Blank lines count toward total but not comments."""
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


def collect(
    repo_root: pathlib.Path, include_headers: bool
) -> list[tuple[pathlib.Path, int, int, float]]:
    rows = []
    for root in ROOTS:
        base = repo_root / root
        if not base.exists():
            continue
        for p in base.rglob("*"):
            if not p.is_file() or not is_source(p, include_headers) or excluded(p):
                continue
            c, t = count_lines(p)
            if t == 0:
                continue
            rows.append((p.relative_to(repo_root), c, t, c / t * 100.0))
    rows.sort(key=lambda r: r[3], reverse=True)
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description="Per-file comment-density linter.")
    ap.add_argument(
        "--threshold",
        type=float,
        default=30.0,
        help="Max allowed per-file comment percentage (default: 30).",
    )
    ap.add_argument(
        "--top",
        type=int,
        default=10,
        help="How many top offenders to print (default: 10).",
    )
    ap.add_argument(
        "--repo-root",
        type=pathlib.Path,
        default=None,
        help="Override repo root (default: walk up from script location).",
    )
    ap.add_argument(
        "--include-headers",
        action="store_true",
        help="Also scan .h/.hpp files. Default: .cpp only — headers carry"
        " a legitimate public-API doc burden and need a separate threshold.",
    )
    args = ap.parse_args()

    if args.repo_root:
        repo_root = args.repo_root.resolve()
    else:
        # scripts/check_comment_density.py → repo root is 3 levels up
        repo_root = pathlib.Path(__file__).resolve().parents[3]

    rows = collect(repo_root, include_headers=args.include_headers)
    if not rows:
        print(f"no source files found under {repo_root}", file=sys.stderr)
        return 1

    over = [r for r in rows if r[3] > args.threshold]
    total_c = sum(r[1] for r in rows)
    total_t = sum(r[2] for r in rows)
    project_pct = total_c / total_t * 100.0 if total_t else 0.0

    print(
        f"[comment-density] scanned {len(rows)} files, "
        f"{total_c}/{total_t} lines comments ({project_pct:.1f}% project-wide)"
    )
    print(f"[comment-density] top {min(args.top, len(rows))} offenders:")
    for path, c, t, pct in rows[: args.top]:
        flag = " FAIL" if pct > args.threshold else ""
        print(f"  {pct:5.1f}%  {c:5d}/{t:5d}  {path}{flag}")

    if over:
        print(
            f"\n[comment-density] {len(over)} file(s) over {args.threshold}% threshold:",
            file=sys.stderr,
        )
        for path, c, t, pct in over:
            print(f"  {pct:5.1f}%  {path}", file=sys.stderr)
        print(
            "\nTrim comments per the policy in .claude/CLAUDE.md "
            "(Code comment policy section).",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
