#!/usr/bin/env python3
"""Open Aseprite with a fresh blank canvas at the given size, ready to draw.

Aseprite's CLI cannot create a new canvas directly, so we pre-make an empty
PNG at the right dimensions in `art/<name>.png` and tell Aseprite to open it.
You draw, hit Ctrl+S, and the file overwrites in place. Then run
`scripts/png_to_sprite.py art/<name>.png --name FOO` to convert.

Usage
-----
    python scripts/new_sprite.py PLAYER 8 12
    python scripts/new_sprite.py ENEMY  5 8

The script does NOT block; Aseprite opens detached.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.stderr.write(
        "ERROR: Pillow not installed. Install with:\n"
        "    pacman -S mingw-w64-x86_64-python-pillow\n"
    )
    sys.exit(1)

ASEPRITE = Path("C:/Program Files/Aseprite/Aseprite.exe")


def main() -> int:
    ap = argparse.ArgumentParser(description="Open Aseprite on a fresh sprite canvas.")
    ap.add_argument("name", help="Sprite name (lowercased + .png appended for filename)")
    ap.add_argument("width", type=int)
    ap.add_argument("height", type=int)
    ap.add_argument(
        "--force", action="store_true",
        help="Overwrite existing art/<name>.png with a blank canvas",
    )
    args = ap.parse_args()

    if not ASEPRITE.exists():
        sys.stderr.write(f"ERROR: Aseprite not found at {ASEPRITE}\n")
        return 1

    repo_root = Path(__file__).resolve().parent.parent
    art_dir = repo_root / "art"
    art_dir.mkdir(exist_ok=True)
    out_path = art_dir / f"{args.name.lower()}.png"

    if out_path.exists() and not args.force:
        print(f"opening existing canvas: {out_path}")
    else:
        # Black-background indexed PNG with a 2-color (black, white) palette.
        # Aseprite respects the palette on open.
        img = Image.new("P", (args.width, args.height), 0)
        img.putpalette([0, 0, 0, 255, 255, 255] + [0] * (256 * 3 - 6))
        img.save(out_path)
        print(f"created blank canvas: {out_path} ({args.width}x{args.height})")

    # Launch Aseprite detached so this script returns immediately.
    subprocess.Popen(
        [str(ASEPRITE), str(out_path)],
        creationflags=getattr(subprocess, "DETACHED_PROCESS", 0),
        close_fds=True,
    )
    print(f"Aseprite launched. Save with Ctrl+S when you're done.")
    print(f"Then run:")
    print(f"    python scripts/png_to_sprite.py {out_path} --name {args.name.upper()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
