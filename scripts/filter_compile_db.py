#!/usr/bin/env python3
"""Write a compile database holding only this repo's own translation units.

CMake exports every target it configures, dependencies included, so the
database describes roughly twice the code anyone here wants analysed. The
entries for fetched dependencies are also the fragile ones: several of them
name sources a dependency GENERATES while building, and a static-analysis job
that only configures never creates those files. cppcheck refuses to load a
project when a single named file is absent, so one unbuilt dependency source
costs the whole run -- and the failure looks identical to a genuine finding.

Analysis tools are pointed at the filtered database instead. What they analyse
does not change; what they no longer depend on is any dependency's build
output.
"""

import argparse
import json
import os
import sys

# Trees whose sources are ours to analyse. Anything else in the database --
# fetched dependencies, vendored third-party code -- is dropped.
OWNED = ("engines", "games")

# Archived imports. Held to the formatting and lint rules of the repo they came
# from, excluded from every other check here, excluded from this one too.
ARCHIVED = (
    "engines/arduboy-legacy",
    "games/rpg-arduboy",
)


def normalise(path, base):
    """Absolute, forward-slashed, lowercased -- comparable across platforms."""
    if not os.path.isabs(path):
        path = os.path.join(base, path)
    return os.path.normpath(path).replace(os.sep, "/").lower()


def is_owned(entry_file, entry_dir, root):
    path = normalise(entry_file, entry_dir)
    root = normalise(root, root)
    if not path.startswith(root + "/"):
        return False
    rel = path[len(root) + 1:]
    if any(rel.startswith(a + "/") for a in ARCHIVED):
        return False
    return any(rel.startswith(o + "/") for o in OWNED)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("database", help="compile_commands.json to read")
    ap.add_argument("-o", "--output", required=True, help="filtered database to write")
    ap.add_argument("--root", default=os.getcwd(), help="repo root (default: cwd)")
    args = ap.parse_args()

    with open(args.database, encoding="utf-8") as handle:
        entries = json.load(handle)

    kept = [e for e in entries if is_owned(e["file"], e.get("directory", args.root), args.root)]

    if not kept:
        print(
            f"error: no owned translation units in {args.database}. "
            f"Looked for sources under {'/, '.join(OWNED)}/ beneath {args.root}.",
            file=sys.stderr,
        )
        return 1

    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(kept, handle, indent=2)

    dropped = len(entries) - len(kept)
    print(f"{args.output}: kept {len(kept)} of {len(entries)} entries ({dropped} dropped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
