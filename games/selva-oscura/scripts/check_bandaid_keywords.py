#!/usr/bin/env python3
"""Comment-keyword linter for Selva Oscura source.

Greps tracked C++ source for keywords that mark deferred work,
bandaid-flavored fixes, OR authoring-tool / vendor pipeline names:

  TODO  FIXME  HACK  XXX  REVIEW
  bandaid  workaround  paper over  papered over
  "for now"  "temporary"  "temp fix"
  MB-Lab  CharMorph  Rigify  Mixamo  Blender  Auto-Rig Pro  ARP
  FBX2glTF  Quaternius  ManuelBastioni  Gaming armature

Per .claude/CLAUDE.md doctrine:
  - Deferred work goes to GitHub issues, not source comments
  - Bandaids are explicitly flagged with an issue + ticket
  - Code comments stay general (explain WHY); they don't reference
    issue numbers (issues get renumbered)
  - Source describes the engine artifact, not the tool that produced it.
    Tool/vendor names rot when the tool gets swapped -- describe rigs as
    rigs, skeletons as skeletons, glbs as glbs. Authoring-tool history
    belongs in docs/ or commit messages, never in shipped source.

If a flagged comment is genuinely temporary work in flight, the
escape hatch is appending the marker to the line:

    // BANDAID(approved): one-line reason this can't be root-fixed
                                 right now -- to be removed when X lands.

Lines with `BANDAID(approved):` are exempt. Anything else fails.

Scope:
  - engines/engine/**/*.{cpp,h}
  - games/selva-oscura/**/*.{cpp,h}
  - games/prison-escape-game/**/*.{cpp,h}

Skipped:
  - engines/arduboy-legacy/  (archived, read-only)
  - games/rpg-arduboy/       (archived, read-only)
  - build/                   (generated)
  - any third-party src in engines/engine/deps/ if present

Exit 0 on clean, 1 on any flagged line.
"""

import io
import re
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT = Path(__file__).resolve().parents[3]

SCAN_ROOTS = [
    REPO_ROOT / "engines" / "engine",
    REPO_ROOT / "games" / "selva-oscura",
    REPO_ROOT / "games" / "prison-escape-game",
]

SKIP_DIRS = {"deps", "third_party", "external", "build", "ozz", "imgui"}

# Each pattern is (regex, label). Word-boundaried where it matters so
# "todoist" doesn't trip the TODO match. Case-insensitive across the
# board -- "Hack", "HACK", "hack" all hit.
PATTERNS = [
    # The TODO/FIXME/HACK/XXX/REVIEW family is convention-tagged in
    # UPPERCASE; lowercase 'review' / 'hack' as plain English words
    # are not deferral markers. Case-sensitive on this group.
    (re.compile(r"\bTODO\b"), "TODO"),
    (re.compile(r"\bFIXME\b"), "FIXME"),
    (re.compile(r"\bHACK\b"), "HACK"),
    (re.compile(r"\bXXX\b"), "XXX"),
    (re.compile(r"\bREVIEW\b"), "REVIEW"),
    # The doctrine words below are caught case-insensitive: their
    # presence anywhere in a comment is the signal regardless of
    # capitalisation.
    (re.compile(r"\bbandaid\b", re.IGNORECASE), "bandaid"),
    (re.compile(r"\bworkaround\b", re.IGNORECASE), "workaround"),
    (re.compile(r"paper(ed)?\s+over", re.IGNORECASE), "paper-over"),
    (re.compile(r"\bfor\s+now\b", re.IGNORECASE), "for-now"),
    (re.compile(r"\btemporary\b", re.IGNORECASE), "temporary"),
    (re.compile(r"\btemp\s+fix\b", re.IGNORECASE), "temp-fix"),
    # Authoring tools / vendor pipelines -- describe the artifact, not
    # the toolchain. These rot the moment a tool gets swapped.
    # Carve-out: referencing on-disk identifiers like `mixamorig:Hips`
    # is allowed (describes real persisted data). The negative lookahead
    # on "Mixamo" excludes "mixamorig:" so identifier references survive.
    (re.compile(r"\bMB[-_ ]?Lab\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bCharMorph\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bRigify\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bMixamo(?!rig|\.com)\b", re.IGNORECASE), "tool-name"),
    # Blender as a standalone word; "scripts/blender/" path refs are
    # citing real on-disk locations and exempt via the negative
    # lookbehind on a slash. "Blender's" / "Blender-authored" still flag.
    (re.compile(r"(?<![/\\])\bBlender\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bAuto[-_ ]?Rig[-_ ]?Pro\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bARP\b"), "tool-name"),
    (re.compile(r"\bFBX2glTF\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bQuaternius\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bManuel[-_ ]?Bastioni\b", re.IGNORECASE), "tool-name"),
    (re.compile(r"\bGaming\s+armature\b", re.IGNORECASE), "tool-name"),
]

EXEMPT_MARKER = re.compile(r"BANDAID\(approved\):")

# A line is considered a "comment line" if it contains // or is inside
# a /* */ block. This linter is intentionally simple: it only flags
# lines whose match appears inside a comment. Identifier matches in
# real code (e.g. a variable literally named "todo_count") are NOT
# flagged. The script does line-local detection -- multi-line /* */
# blocks: we scan for /* ... */ block boundaries and treat lines
# inside the block as comment lines too.

LINE_COMMENT_RE = re.compile(r"//.*$")
BLOCK_OPEN_RE = re.compile(r"/\*")
BLOCK_CLOSE_RE = re.compile(r"\*/")


def iter_source_files():
    for root in SCAN_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in {".cpp", ".h", ".hpp", ".cc"}:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            yield path


def comment_text_for_line(line: str, in_block: bool) -> str:
    """Return whatever portion of the line is inside a comment, or "" if none.
    Also returns updated in_block state for the next line.
    """
    parts = []
    i = 0
    n = len(line)
    while i < n:
        if in_block:
            close = line.find("*/", i)
            if close == -1:
                parts.append(line[i:])
                return "\n".join(parts), True
            parts.append(line[i:close])
            i = close + 2
            in_block = False
            continue
        open_block = line.find("/*", i)
        open_line = line.find("//", i)
        if open_line != -1 and (open_block == -1 or open_line < open_block):
            parts.append(line[open_line:])
            return "\n".join(parts), False
        if open_block != -1:
            i = open_block + 2
            in_block = True
            continue
        return "\n".join(parts), False
    return "\n".join(parts), in_block


def scan_file(path: Path):
    """Yield (line_no, label, line_text) for each violation in this file."""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return
    in_block = False
    for line_no, line in enumerate(text.splitlines(), start=1):
        comment, in_block = comment_text_for_line(line, in_block)
        if not comment:
            continue
        if EXEMPT_MARKER.search(comment):
            continue
        for pattern, label in PATTERNS:
            if pattern.search(comment):
                yield line_no, label, line.rstrip()
                break  # one finding per line is enough


def main():
    violations = []
    for path in iter_source_files():
        for line_no, label, line in scan_file(path):
            rel = path.relative_to(REPO_ROOT).as_posix()
            violations.append((rel, line_no, label, line))

    if not violations:
        print("check_bandaid_keywords: clean (0 violations)")
        return 0

    print(f"check_bandaid_keywords: {len(violations)} violation(s)")
    print("")
    print("Each finding marks a deferred-work / bandaid keyword in a")
    print("comment. Per .claude/CLAUDE.md doctrine these belong in")
    print("GitHub issues, not source comments. If the comment is")
    print("genuinely load-bearing in-flight work, append")
    print("'BANDAID(approved):' to the line to exempt it.")
    print("")
    for rel, line_no, label, line in violations:
        print(f"  {rel}:{line_no} [{label}] {line.strip()[:120]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
