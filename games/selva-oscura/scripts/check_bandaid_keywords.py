#!/usr/bin/env python3
"""Comment-keyword linter for Selva Oscura source.

Greps tracked C++ source for keywords that mark deferred work,
bandaid-flavored fixes, authoring-tool / vendor pipeline names, OR
in-progress-codebase-state version labels:

  TODO  FIXME  HACK  XXX  REVIEW
  bandaid  workaround  paper over  papered over
  "for now"  "temporary"  "temp fix"
  MB-Lab  CharMorph  Rigify  Mixamo  Blender  Auto-Rig Pro  ARP
  FBX2glTF  Quaternius  ManuelBastioni  Gaming armature
  v1  v2  v3 ...  V1  V2 ...  "version 1" / "version 2" ...

Per .claude/CLAUDE.md doctrine:
  - Deferred work goes to GitHub issues, not source comments
  - Bandaids are explicitly flagged with an issue + ticket
  - Code comments stay general (explain WHY); they don't reference
    issue numbers (issues get renumbered)
  - Source describes the engine artifact, not the tool that produced it.
    Tool/vendor names rot when the tool gets swapped -- describe rigs as
    rigs, skeletons as skeletons, glbs as glbs. Authoring-tool history
    belongs in docs/ or commit messages, never in shipped source.
  - "v1 / v2 / version 2" framing describes the in-progress *codebase*
    state ("v1 is imperative; goes data-driven once X"), not a real
    artifact. The next refactor invalidates the label, leaving a comment
    that lies. Describe what the code DOES today; design history belongs
    in commit messages or docs/.

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
    # Codebase-state version labels. "v1 is imperative; goes data-driven
    # once X" rots the moment X lands.
    #
    # The pattern fires on `vN` standing alone in prose:
    #   "for v1 we ignore..."  →  flags
    #   "v1 ships..."          →  flags
    #   "in v2 this is..."     →  flags
    #   "cognition-system v1." →  flags (sentence-terminator period OK)
    #
    # Excludes code-shorthand identifier references where `vN` is glued
    # to identifier extension characters (`/`, `_`, `-`) before OR after:
    #   "tri_v0/v1/v2"         →  skipped (vertex-index shorthand)
    #   "u0/v0, u1/v1"         →  skipped (UV-coord shorthand)
    #   "node_v3 child"        →  skipped (identifier reference)
    #
    # `.` is intentionally NOT in the trailing exclusion -- a period
    # after vN is a sentence terminator, not an identifier extension.
    # Wiki-links like [[cognition-system-v1]] are stripped before this
    # pattern runs (see WIKILINK_RE). External-spec versions like
    # "glTF 2.0" / "OpenGL 4.3" / "version 2 of the spec" still match
    # the literal "version N" form below -- intentional: prefer the
    # specific name ("the glTF spec", "OpenGL 4.3") over "version 2".
    (re.compile(r"(?<![/_\-])\bv\d+\b(?![/_\-])", re.IGNORECASE), "version-label"),
    (re.compile(r"\bversion\s+\d+\b", re.IGNORECASE), "version-label"),
]

EXEMPT_MARKER = re.compile(r"BANDAID\(approved\):")

# Memory wiki-links like [[cognition-system-v1]] reference real memory
# file slugs and are not codebase-state version labels. Stripped from
# comment text before pattern matching so the version-label rule only
# fires on prose like "v1 is imperative".
WIKILINK_RE = re.compile(r"\[\[[^\]]*\]\]")

# Save-file / archetype-schema version context. When a comment names
# any of these words, the vN tokens in the same line describe a real
# persisted-data schema version (save file shape, JSON archetype
# schema) -- not the codebase state. The version IS real and the
# comment is correctly describing migration / back-compat behavior.
SCHEMA_VERSION_CONTEXT_RE = re.compile(
    r"\b(save|saves|schema|migration|migrate|persisted|serialize|"
    r"deserialize|legacy|archetype|archetypes)\b",
    re.IGNORECASE,
)

# Per-line shapes that ARE migration code regardless of nearby words:
#   "v5 -> v6: ..."  / "v7 -> v8: ..."
#   "v6 or earlier"  / "v8 saves"
# These describe real persisted-version transitions, not codebase state.
MIGRATION_LINE_RE = re.compile(
    r"\bv\d+\s*(?:->|-->)|\bv\d+\b\s+(?:or earlier|saves?)\b|"
    r"\b(?:saves?|schema)\s+v\d+\b",
    re.IGNORECASE,
)

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
        # Strip memory wiki-links before scanning so [[name-v1]]
        # references to real memory file slugs don't trip the
        # version-label rule.
        scan_text = WIKILINK_RE.sub("", comment)
        # Save-file / schema migration comments describe real persisted
        # versions, not codebase state. Exempt the version-label rule
        # (but tool-name / TODO / etc. still fire as usual). Two shapes
        # of evidence: a keyword like "save"/"schema"/"migration" in
        # the comment, OR a per-line shape like "v5 -> v6" or "v8 saves".
        in_schema_context = (bool(SCHEMA_VERSION_CONTEXT_RE.search(scan_text))
                             or bool(MIGRATION_LINE_RE.search(scan_text)))
        for pattern, label in PATTERNS:
            if in_schema_context and label == "version-label":
                continue
            if pattern.search(scan_text):
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
