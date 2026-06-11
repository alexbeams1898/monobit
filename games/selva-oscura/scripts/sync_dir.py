#!/usr/bin/env python3
"""Robust recursive directory sync used by the selva-oscura CMake build.

CMake's `copy_directory` has historically failed on the GitHub Actions
Ubuntu runner when the destination tree already partially exists from
a concurrent bake step. This script is a stable, equivalent replacement
that overwrites destination files in place without complaining about
existing parents.

Usage: sync_dir.py <source> <destination>
"""

import os
import shutil
import sys


def _ensure_dir(path: str) -> None:
    """Make `path` a directory. If anything else exists at that path
    (regular file, symlink, broken link), replace it.

    Also probes the path's PARENT for the same issue -- on CI the
    failure mode is that the parent (e.g. ``bin/selva-oscura``) ended
    up as a regular file from a stray earlier write, so a plain
    ``isfile``/``exists`` check on the target path returns false yet
    ``os.makedirs`` blows up with NotADirectoryError on a parent
    component. Walk parents and unlink anything that's not a dir."""
    parts = []
    cur = path
    while cur and cur != os.path.sep and not os.path.isdir(cur):
        parts.append(cur)
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    for p in reversed(parts):
        if os.path.isdir(p):
            continue
        if os.path.lexists(p):
            print(f"sync_dir: replacing non-dir at {p} with a directory",
                  file=sys.stderr)
            try:
                os.unlink(p)
            except OSError:
                # Symlink-to-directory or other weird state; fall back
                # to removing recursively.
                import shutil as _shutil
                _shutil.rmtree(p, ignore_errors=True)
        os.makedirs(p, exist_ok=True)


def sync(src: str, dst: str) -> None:
    if not os.path.isdir(src):
        print(f"sync_dir: source is not a directory: {src}", file=sys.stderr)
        sys.exit(1)
    _ensure_dir(dst)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        out_dir = os.path.join(dst, rel) if rel != "." else dst
        _ensure_dir(out_dir)
        for name in files:
            src_file = os.path.join(root, name)
            dst_file = os.path.join(out_dir, name)
            if os.path.isdir(dst_file):
                shutil.rmtree(dst_file)
            try:
                shutil.copy2(src_file, dst_file)
            except OSError as e:
                print(f"sync_dir: copy failed: {src_file} -> {dst_file}: {e}",
                      file=sys.stderr)
                sys.exit(1)


def main() -> None:
    if len(sys.argv) != 3:
        print("Usage: sync_dir.py <source> <destination>", file=sys.stderr)
        sys.exit(2)
    sync(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
