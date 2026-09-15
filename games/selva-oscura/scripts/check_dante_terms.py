#!/usr/bin/env python3
"""Dante-Inferno name-leak linter for Selva Oscura source.

Selva Oscura is a *far-descendant* of Dante's Inferno — the machinery
broke centuries ago, the residents mutated, the cosmology drifted.
Every named thing from the *Commedia* either devolved into something
unrecognizable, was renamed by the in-fiction residents, or was
forgotten. A player who knows the *Inferno* should sense parallels;
they should NEVER see the source names on-screen or in-source.

This linter flags Dante-Inferno-specific proper nouns in source /
config / docs. Add terms as they surface — the list is intentionally
starter-shaped, not exhaustive.

Grandfathered: **Beatrice** — a load-bearing cosmological figure with
extensive prior lore (setting.md, classes.md, companions.md, etc.);
she stays canonical to Selva.

Exemption marker:

    // INFERNO(exempt): reason -- this is a doc that legitimately cites the source

Reference texts (`docs/reference/*.txt`) and the private `.claude/`
directory are exempt whole-file.

Scope:
  - engines/engine/**/*.{cpp,h,md,json}
  - games/selva-oscura/**/*.{cpp,h,md,json,py}
  - games/prison-escape-game/**/*.{cpp,h,md,json,py}

Skipped:
  - engines/arduboy-legacy/  (archived)
  - games/rpg-arduboy/       (archived)
  - build/                   (generated)
  - .claude/                 (private)
  - docs/reference/          (canonical source texts)
  - node_modules/, __pycache__/

Warning mode (default): prints findings, exits 0. Use during rename
passes so it does not block work with pre-existing violations.
Error mode: `--error`. Use in CI / merge-gates once the codebase is
clean.
"""

import argparse
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

SKIP_DIRS = {
    "deps", "third_party", "external", "build", "ozz", "imgui",
    "node_modules", "__pycache__", ".git",
    ".claude",           # private notes / memory
    "arduboy-legacy",    # archived
    "rpg-arduboy",       # archived
    "reference",         # docs/reference/ holds canonical source texts
}

SCAN_SUFFIXES = {".cpp", ".h", ".hpp", ".cc", ".md", ".json", ".py"}

# Dante-Inferno proper nouns forbidden in Selva source. Word-boundaried
# so "Cocytus" catches "Cocytus" but not a substring. Case-insensitive.
# Extend as terms come up.
FORBIDDEN_TERMS = [
    # Named circles / regions of Hell
    "Limbo",
    "Malebolge",
    "Cocytus",
    "Antenora",
    "Caina",
    "Ptolomea",
    "Judecca",
    # Named residents (canonical Canto IV honored pagans + notable figures)
    "Aristotle",
    "Plato",
    "Socrates",
    "Homer",
    "Ovid",
    "Horace",
    "Lucan",
    "Virgil",
    "Caesar",
    "Saladin",
    "Averroes",
    "Galen",
    "Hippocrates",
    "Democritus",
    "Diogenes",
    "Anaxagoras",
    "Thales",
    "Empedocles",
    "Heraclitus",
    "Zeno",
    "Cicero",
    "Seneca",
    "Euclid",
    "Ptolemy",
    # Rivers
    "Acheron",
    "Styx",
    "Phlegethon",
    "Lethe",
    # Guides / ferrymen / gate-keepers
    "Charon",
    "Minos",
    "Cerberus",
    "Plutus",
    "Phlegyas",
    "Geryon",
    # Places
    # (Add "Dis" only if it becomes a leak; too short for safe wordboundary)
    # Dante himself
    "Dante",
    "Alighieri",
]

# Grandfathered: allowed in Selva source.
GRANDFATHERED = {"Beatrice"}

FORBIDDEN_RE = re.compile(
    r"\b(" + "|".join(re.escape(t) for t in FORBIDDEN_TERMS) + r")\b",
    re.IGNORECASE,
)

EXEMPT_MARKER = re.compile(r"INFERNO\(exempt\):")


def iter_source_files():
    for root in SCAN_ROOTS:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in SCAN_SUFFIXES:
                continue
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            yield path


def scan_file(path: Path):
    """Yield (line_no, term, line_text) for each violation in this file."""
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return
    for line_no, line in enumerate(text.splitlines(), start=1):
        if EXEMPT_MARKER.search(line):
            continue
        for match in FORBIDDEN_RE.finditer(line):
            term = match.group(1)
            yield line_no, term, line.rstrip()
            break  # one finding per line is enough


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--error", action="store_true",
                    help="Exit 1 on any finding. Default is warning-only (exit 0).")
    args = ap.parse_args()

    violations = []
    for path in iter_source_files():
        for line_no, term, line in scan_file(path):
            rel = path.relative_to(REPO_ROOT).as_posix()
            violations.append((rel, line_no, term, line))

    if not violations:
        print("check_dante_terms: clean (0 findings)")
        return 0

    mode = "ERROR" if args.error else "WARN"
    print(f"check_dante_terms: {len(violations)} finding(s) [{mode}]")
    print("")
    print("Each finding names a Dante-Inferno proper noun in Selva source.")
    print("Selva must not leak the source-material names. Rename to a")
    print("Selva-native term. If the line is a legitimate reference (e.g. a")
    print("design doc that must cite the Commedia), append")
    print("'INFERNO(exempt): reason' to that line.")
    print("")
    for rel, line_no, term, line in violations:
        print(f"  {rel}:{line_no} [{term}] {line.strip()[:120]}")

    return 1 if args.error else 0


if __name__ == "__main__":
    sys.exit(main())
