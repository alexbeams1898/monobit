#!/usr/bin/env python3
"""Point of Entry's art pipeline: Aseprite sources in, game assets out.

    python tools/art.py            # build everything once
    python tools/art.py --watch    # build, then rebuild whatever you save

WHY THIS EXISTS. Aseprite is very good at drawing and nothing else in the chain is worth
doing by hand: exporting, packing, writing down where each frame landed, and keeping the
game's idea of a sprite in step with the file it came from. Every one of those is a place
for the art and the config to drift apart, and drift is the bug you find in play rather
than at build.

So: one command watches art/, and saving a file is the only action. There is no export
step to remember, no atlas to repack, no numbers to copy into a config.

THE SOURCE OF TRUTH IS THE .aseprite FILE. Frame counts, animation names, and pivots are
authored where the art is authored -- Aseprite's own tags and slices -- and read back out
here. Nothing about a sprite is written down twice.

  art/characters/player.aseprite   ->  assets/sprites/player.png
                                       assets/sprites/player.json

The json records what the game cannot infer from a PNG: the size of a frame, which frames
belong to which animation, and where the character's feet are.

BOTH SIDES ARE COMMITTED -- the .aseprite sources AND the exported sheets. Generated output is
normally left out of a repo, but the game loads these at runtime and the build does not run this
script, so ignoring them would mean a fresh clone has no art until someone installs a licensed
copy of Aseprite. Re-export after editing a source; the watcher does it for you.

DRAW EVERY CHARACTER FACING RIGHT. There is one drawing per creature and the game mirrors it
to face left -- no back sprite, no up or down pose. Right is the direction that needs no
correction anywhere in the code, so a sprite drawn facing left is a bug rather than a
preference. If one already exists, correct the source once:

    aseprite -b art/characters/foo.aseprite --script tools/flip.lua
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

# Aseprite ships a CLI; this is where it lands on a default Windows install. Overridable so
# a different install (or a Linux box) does not need the script edited.
DEFAULT_ASEPRITE = r"C:/Program Files/Aseprite/aseprite.exe"

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "art"
OUT = ROOT / "assets" / "sprites"


def find_aseprite(explicit: str | None) -> str:
    for candidate in (explicit, DEFAULT_ASEPRITE):
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit(
        "art: cannot find aseprite.exe. Pass --aseprite <path>, or install it to\n"
        f"     {DEFAULT_ASEPRITE}"
    )


def sources() -> list[Path]:
    return sorted(ART.rglob("*.aseprite")) if ART.is_dir() else []


def export(aseprite: str, src: Path) -> dict | None:
    """Export one .aseprite to a sheet + its metadata. Returns what the game needs."""
    OUT.mkdir(parents=True, exist_ok=True)
    sheet = OUT / f"{src.stem}.png"
    raw = OUT / f"{src.stem}.aseprite-data.json"

    # --list-tags / --list-slices / --list-layers force those sections into the data file even
    # when the sprite has none, so the reader below never has to guess whether a key is missing
    # because there are no tags or because the export forgot them.
    cmd = [
        aseprite, "-b", str(src),
        "--sheet", str(sheet),
        "--sheet-type", "horizontal",   # frames left to right: one row, easy to index
        "--data", str(raw),
        "--format", "json-array",
        "--list-tags",
        "--list-slices",
        "--list-layers",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"art: {src.name} FAILED\n{result.stderr.strip()}")
        return None

    meta = json.loads(raw.read_text(encoding="utf-8"))
    raw.unlink()  # the game reads the digested version below, not Aseprite's

    frames = meta["frames"]
    if not frames:
        print(f"art: {src.name} has no frames")
        return None

    # Every frame is the same size -- Aseprite exports the canvas, not the trimmed art --
    # so the first frame's box describes them all.
    first = frames[0]["frame"]
    out = {
        "sheet": f"assets/sprites/{sheet.name}",
        "frame_w": first["w"],
        "frame_h": first["h"],
        "frames": len(frames),
        # Per-frame durations in ms, as authored on the timeline. The game does not have to
        # guess a frame rate, and a held pose stays held.
        "durations": [f["duration"] for f in frames],
    }

    # TAGS are the animation names. "walk" over frames 0-3 means the game asks for "walk"
    # rather than knowing a magic index.
    tags = {}
    for t in meta["meta"].get("frameTags", []) or []:
        tags[t["name"]] = {"from": t["from"], "to": t["to"]}
    if tags:
        out["anims"] = tags

    # FACING. Characters are drawn facing right and the game mirrors them; art facing the wrong
    # way looks fine in Aseprite and is only noticed in play, as a character who moonwalks. The
    # file cannot be inspected for which way a face points, so it is asserted instead: a layer
    # named "facing-left" marks art that needs correcting, and this says so at export rather
    # than letting it ship. Correct it with tools/flip.lua, do not compensate in code.
    layers = [l.get("name", "") for l in meta["meta"].get("layers", []) or []]
    if any(name.strip().lower() == "facing-left" for name in layers):
        print(f"art: {src.name} WARNING -- marked facing-left. Characters are drawn facing "
              f"right.\n     Fix the source once:  aseprite -b {src} --script tools/flip.lua")

    # SLICES carry pivots. A slice named "feet" says where the character stands, which is
    # what a sprite has to be positioned and depth-sorted by -- not its centre, and not the
    # bottom of its canvas.
    for sl in meta["meta"].get("slices", []) or []:
        key = sl.get("keys", [{}])[0]
        bounds = key.get("bounds")
        if sl["name"] == "feet" and bounds:
            out["anchor_x"] = bounds["x"] + bounds["w"] // 2
            out["anchor_y"] = bounds["y"] + bounds["h"]

    (OUT / f"{src.stem}.json").write_text(json.dumps(out, indent=4) + "\n", encoding="utf-8")

    anim = f", anims: {', '.join(tags)}" if tags else ""
    print(f"art: {src.name} -> {out['frame_w']}x{out['frame_h']} "
          f"x{out['frames']}{anim}")
    return out


def build_all(aseprite: str) -> int:
    files = sources()
    if not files:
        print(f"art: nothing to build -- no .aseprite files under {ART}")
        return 0
    for f in files:
        export(aseprite, f)
    return len(files)


def watch(aseprite: str) -> None:
    """Rebuild whatever changes. Polling rather than filesystem events: a handful of files
    checked twice a second costs nothing, and it behaves the same on every platform and
    over network drives, where event APIs quietly do not."""
    print("art: watching. Save in Aseprite and it lands in the game. Ctrl-C to stop.")
    seen: dict[Path, float] = {f: f.stat().st_mtime for f in sources()}
    build_all(aseprite)
    try:
        while True:
            time.sleep(0.5)
            for f in sources():
                mtime = f.stat().st_mtime
                if seen.get(f) != mtime:
                    seen[f] = mtime
                    export(aseprite, f)
    except KeyboardInterrupt:
        print("\nart: stopped")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--watch", action="store_true", help="rebuild on save")
    ap.add_argument("--aseprite", help="path to aseprite.exe")
    args = ap.parse_args()

    aseprite = find_aseprite(args.aseprite)
    if args.watch:
        watch(aseprite)
    else:
        build_all(aseprite)


if __name__ == "__main__":
    main()
