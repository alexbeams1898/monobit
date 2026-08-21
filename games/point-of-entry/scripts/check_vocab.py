#!/usr/bin/env python3
"""Vocabulary lint: one thing, one word -- and the words still reach what they name.

Four checks. The first is the vocabulary itself; the other three exist because a rename
that misses a JSON key, a path, or the map does NOT fail to compile. Each of them caught
something a careful read had already been over:

  1. RETIRED WORDS.  seep/site, creature/vermin, bestiary, floorgen, and Floor meaning a
     room. Permitted only where a save's history is named -- see ALLOW below.
  2. CONFIG KEYS.    Every key in config/ against every key the code asks a document for.
     A key nothing reads is a rename that stopped at the source: the value silently
     becomes a default, and a floor quietly generates to a different shape.
  3. PATHS.          Every config/ or assets/ path named in config or source, against
     what is actually on disk.
  4. MAP BUILDERS.   Every entity the map places against every builder registered for
     one. An authored thing with no builder simply never exists.

See docs/design/VOCABULARY.md. Run from the repo root; exits 1 with a list.
"""

from __future__ import annotations

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = [p for p in list((ROOT / "src").rglob("*.cpp")) + list((ROOT / "include").rglob("*.h"))]
TESTS = list((ROOT / "tests").rglob("*.cpp"))
CONFIGS = sorted((ROOT / "config").rglob("*.json"))
MAP = ROOT / "assets/maps/world.ldtk"

# Migration code and its fixtures MUST name the old spellings: the left-hand column of a
# migration table is history, and a fixture written in today's words tests nothing.
ALLOW = {"SaveGame.cpp", "save_test.cpp", "VOCABULARY.md", "check_vocab.py"}

RETIRED = {
    "seep": "hole",
    "Seep": "Hole",
    "vermin": "pest",
    "Vermin": "Pest",
    "bestiary": "field guide (or formulas, for the derivation table)",
    "Bestiary": "FieldGuide (or Formulas)",
    "floorgen": "roomgen",
    "FloorGen": "RoomGen",
}


def retired_words(problems: list[str]) -> None:
    for p in SRC + TESTS + CONFIGS + [MAP]:
        if p.name in ALLOW or not p.exists():
            continue
        text = p.read_text(encoding="utf-8", errors="ignore")
        for word, say in RETIRED.items():
            if re.search(r"\b%s\b" % word, text):
                problems.append(f"{p.relative_to(ROOT)}: '{word}' is retired -- say {say}")


def asked_keys() -> set[str]:
    """Every config key the code could be asking for.

    Not just `.value("key")`: a key can be a NAME the code looks up at a call site --
    sound::play("footstep") -- rather than a field it reads off a document. So any bare
    string literal in the source counts. That is looser, but it still catches what this
    check exists for: a key renamed in code and not in the file leaves the OLD spelling
    nowhere in the source at all.
    """
    asked: set[str] = set()
    for p in SRC:
        text = p.read_text(encoding="utf-8")
        asked |= set(re.findall(r'"([A-Za-z_]\w*)"', text))
    return asked


def config_keys(problems: list[str]) -> None:
    asked = asked_keys()

    def keys(node, out: set[str]) -> None:
        if isinstance(node, dict):
            for k, v in node.items():
                if k != "_comment":
                    out.add(k)
                    keys(v, out)
        elif isinstance(node, list):
            for v in node:
                keys(v, out)

    for p in CONFIGS:
        found: set[str] = set()
        keys(json.loads(p.read_text(encoding="utf-8")), found)
        # Single characters and bare numbers are DATA -- marker letters, tile ids -- and are
        # read by walking the object rather than by name.
        orphans = sorted(k for k in found if k not in asked and len(k) > 1 and not k.isdigit())
        for k in orphans:
            problems.append(f"{p.relative_to(ROOT)}: nothing reads '{k}' -- a rename that "
                            f"stopped at the source, so the value is silently a default")


def paths_exist(problems: list[str]) -> None:
    def check(value: str, where: Path) -> None:
        if value.startswith(("config/", "assets/")) and "*" not in value:
            if not (ROOT / value).exists():
                problems.append(f"{where.relative_to(ROOT)}: names '{value}', which is not there")

    def walk(node, where: Path) -> None:
        if isinstance(node, str):
            check(node, where)
        elif isinstance(node, dict):
            for v in node.values():
                walk(v, where)
        elif isinstance(node, list):
            for v in node:
                walk(v, where)

    for p in CONFIGS:
        walk(json.loads(p.read_text(encoding="utf-8")), p)
    # Tests name absent files on purpose, and migration code names directories that MOVED --
    # the whole job of a migration table is to know where things used to be.
    for p in SRC:
        if p.name in ALLOW:
            continue
        for m in re.findall(r'"((?:config|assets)/[\w./-]+)"', p.read_text(encoding="utf-8")):
            check(m, p)


def snake(name: str) -> str:
    out = []
    for i, c in enumerate(name):
        if c.isupper() and i:
            out.append("_")
        out.append(c.lower())
    return "".join(out)


def map_builders(problems: list[str]) -> None:
    if not MAP.exists():
        return
    registered = set()
    for p in SRC:
        registered |= set(re.findall(r'registerBuilder\(\s*\n?\s*"(\w+)"',
                                     p.read_text(encoding="utf-8")))
    placed: dict[str, set[str]] = defaultdict(set)
    world = json.loads(MAP.read_text(encoding="utf-8"))
    for level in world["levels"]:
        for layer in level.get("layerInstances", []):
            for entity in layer.get("entityInstances", []):
                placed[snake(entity["__identifier"])].add(level["identifier"])
    for kind, levels in sorted(placed.items()):
        if kind not in registered:
            problems.append(f"world.ldtk: '{kind}' is placed in {sorted(levels)} and no builder "
                            f"handles it -- it will simply not exist")


def main() -> int:
    problems: list[str] = []
    retired_words(problems)
    config_keys(problems)
    paths_exist(problems)
    map_builders(problems)
    if problems:
        print(f"check_vocab: {len(problems)} violation(s)")
        for p in problems:
            print(f"  {p}")
        return 1
    print("check_vocab: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
