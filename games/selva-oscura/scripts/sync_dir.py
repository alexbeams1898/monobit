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


def sync(src: str, dst: str) -> None:
    if not os.path.isdir(src):
        print(f"sync_dir: source is not a directory: {src}", file=sys.stderr)
        sys.exit(1)
    os.makedirs(dst, exist_ok=True)
    for root, dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        out_dir = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(out_dir, exist_ok=True)
        for name in files:
            src_file = os.path.join(root, name)
            dst_file = os.path.join(out_dir, name)
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
