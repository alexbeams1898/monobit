#!/usr/bin/env python3
"""Comment-content linter.

The density linter measures how MUCH a file says. This one measures what it is
allowed to say. Both are needed: a file can sit well under its ratio and still
carry a comment naming a tool that was replaced two years ago.

The rules, and why each one rots:

  authoring tools   The chain of tools that PRODUCED an asset is not a property
                    of the asset. Swap the tool and every comment naming it
                    becomes a lie, where "the humanoid rig" survives. A file
                    format the code parses is a different thing and is not on
                    this list -- naming it names the data.
  game names        Design vocabulary belongs in docs/. Source describes the
                    mechanism, not the genre it is borrowed from.
  version labels    "v1" is only meaningful beside a v2 that may never exist,
                    and means nothing to a reader who arrived after both.
  change history    A comment arguing the current code is right is the author
                    talking to a reviewer. The next reader never saw the old
                    version, so it defends against a ghost.
  note links        [[some_note]] cites a file that is not in this repo.

ON-DISK IDENTIFIERS ARE EXEMPT. A comment citing a real string the code reads
-- a bone named mixamorig:Hips, a .ldtk file, an .aseprite source -- describes
the persisted data, not the workflow. The test is whether the comment would
still make sense with the name deleted; if not, the name stays.

Escape hatch, for a line that genuinely needs the word:

    // ALLOWED: <reason>  -- this line is exempt

Usage:
    python scripts/check_comment_content.py
    python scripts/check_comment_content.py --warn     # report, exit 0
"""

from __future__ import annotations

import argparse
import re
import sys

import sourcetrees

RULES = {
    # Tools that PRODUCED an asset. Formats the code parses -- LDtk, Aseprite --
    # are deliberately absent: naming those names the data, not the pipeline.
    "authoring tool": (
        r"\b(MB-?Lab|CharMorph|Rigify|Auto-?Rig ?Pro|FBX2glTF|Quaternius|"
        r"ManuelBastioni|MPFB2?|Mixamo|Blender|Substance (?:Painter|Designer|3D))\b"
    ),
    # Case-SENSITIVE: "souls" is this game's own vocabulary (damned souls,
    # unjudged souls) and "doom" is an ordinary verb. Only the
    # series-as-a-yardstick senses are listed, never the bare noun.
    "game or genre name": (
        r"(\bDark Souls\b|\bElden Ring\b|\bVampire Survivors\b|\bHollow Knight\b"
        r"|\bUndertale\b|\bEarth[Bb]ound\b|\bMushishi\b|\bDiablo\b|\bDoom\b"
        r"|\bOblivion\b|\bPokemon\b|[Ss]ouls-?like\b"
        r"|[Ss]ouls[-/ ](?:style|series|class|convention|feel|games?|rule|UX|ER|blend)\b"
        r"|\brogue-?like\b|\bVS-style\b)"
    ),
    "version label": r"\b([vV][12]\b(?!\.)|version [12]\b|[Ss]print \d+)",
    "change history": (
        r"\b(legacy behaviou?r|preserves legacy|back-?compat|backwards compat|"
        r"the old way|used to be|we tried|instead of the old|rather than the old)\b"
    ),
    # Any wiki-style link. None of these resolve to anything in this repo.
    "note link": r"\[\[[A-Za-z][\w-]*[_-][\w-]+\]\]",
    "design status": r"\b(LOCKED \d|PARKED\b|DEFERRED\b|per the locked design)\b",
}

# Proper nouns stay proper nouns however they were typed.
CASELESS = {"authoring tool"}

EXEMPT = re.compile(r"\bALLOWED:")

# A version label in migration vocabulary is versioning DATA -- a fact about
# files that exist, not a label on a phase of the code.
SCHEMA_VERSION = re.compile(
    r"(migrat|schema[ _]version|save ?file|savegame|save format|older save|"
    r"written by|read back)", re.IGNORECASE)

# v0/v1/v2 and u0/u1 are vertex and UV components. Two or more of them in one
# place is geometry vocabulary, not a version.
VERTEX_NAMES = re.compile(r"\b[uv][0-3]\b")

# A name beside a real extension, a path, or a quoted identifier is naming data
# on disk rather than narrating a pipeline.
ON_DISK = re.compile(
    r"(\.(ldtk|aseprite|blend|glb|gltf|fbx|ozz|mhclo|json|png|ogg)\b"
    r"|[\w-]+/[\w./-]+"
    r"|`[^`]+`"
    r"|\"[^\"]+\""
    r"|\bmixamorig:)",
    re.IGNORECASE,
)


def violations(root=None):
    found = []
    for path in sourcetrees.source_files(root):
        # A file that defines a schema version talks about schema versions.
        # Its "v1" names a document on disk, not a phase of the code.
        try:
            versions_data = "schema_version" in path.read_text(encoding="utf-8",
                                                               errors="replace")
        except OSError:
            versions_data = False
        for block in sourcetrees.comment_blocks(path):
            context = " ".join(t for _n, t in block)
            for line_no, text in block:
                if EXEMPT.search(text):
                    continue
                for rule, pattern in RULES.items():
                    flags = re.IGNORECASE if rule in CASELESS else 0
                    m = re.search(pattern, text, flags)
                    if not m:
                        continue
                    if rule == "authoring tool" and ON_DISK.search(text):
                        continue
                    if rule == "version label" and (versions_data
                                                    or SCHEMA_VERSION.search(context)):
                        continue
                    if rule == "version label" and len(VERTEX_NAMES.findall(text)) >= 2:
                        continue
                    found.append((path, line_no, rule, m.group(0), text))
                    break
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--warn", action="store_true",
                    help="Report and exit 0. For a tree not yet clean.")
    ap.add_argument("--rule", help="Only report this rule.")
    args = ap.parse_args()

    root = sourcetrees.repo_root()
    found = violations(root)
    if args.rule:
        found = [f for f in found if f[2] == args.rule]

    by_rule: dict[str, int] = {}
    for _p, _n, rule, _hit, _t in found:
        by_rule[rule] = by_rule.get(rule, 0) + 1

    if not found:
        print("[comment-content] clean")
        return 0

    for path, line_no, rule, hit, text in found:
        rel = path.relative_to(root).as_posix()
        snippet = text[:88] + ("..." if len(text) > 88 else "")
        print(f"{rel}:{line_no}: {rule} ({hit!r})\n    {snippet}")

    print(f"\n[comment-content] {len(found)} line(s):", file=sys.stderr)
    for rule, n in sorted(by_rule.items(), key=lambda kv: -kv[1]):
        print(f"    {n:4d}  {rule}", file=sys.stderr)
    print("\nRewrite so the comment survives the thing it names being replaced.\n"
          "A line that genuinely needs the word takes an `ALLOWED: <reason>` marker.",
          file=sys.stderr)
    return 0 if args.warn else 1


if __name__ == "__main__":
    sys.exit(main())
