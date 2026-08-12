#!/usr/bin/env python3
"""Structure lint: the source tree keeps the reference layout, mechanically.

The taxonomy (established by prison-escape-game, adopted here strictly):

  include/            app spine only (worldgen, top-level loaders) -- allowlisted
  include/ecs/        components and balance/config -- leaf headers, include nothing game-side
                      outside ecs/
  include/systems/    per-frame logic; files end in System.h
  include/ops/        stateless helper operations
  include/renderers/  draw-only surfaces; files end in Renderer.h
  include/screens/    full-screen UI states
  src/                mirrors include/ exactly, plus main.cpp

Checked by machine because taxonomy enforced by memory decays one convenient exception at a
time. Run from the repo root; exits 1 with a list of violations.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = ROOT / "include"
SRC = ROOT / "src"

ALLOWED_DIRS = {"ecs", "ops", "renderers", "screens", "systems"}
# The spine: worldgen and future top-level loaders. Additions here should be rare and deliberate.
TOP_HEADERS = {"FloorGen.h", "SpriteDefLoader.h", "AreaLoader.h", "SaveGame.h"}
TOP_SOURCES = {"FloorGen.cpp", "SpriteDefLoader.cpp", "AreaLoader.cpp", "SaveGame.cpp",
               "main.cpp"}
SUFFIX_RULES = {"systems": "System.h", "renderers": "Renderer.h"}
# ops are operations or utilities and say which; screens are either a screen (XScreen, XDialog,
# XMenu) or shared screen infrastructure (ScreenX) -- the reference uses both shapes.
OPS_OK = re.compile(r"(Ops|Utils)\.h$")
SCREEN_OK = re.compile(r"(Screen|Dialog|Menu)\.h$|^Screen[A-Z]")
# ecs/ is a leaf: game-side includes must stay inside ecs/.
ECS_FORBIDDEN = re.compile(r'#include\s+"(systems|screens|renderers|ops)/')


def main() -> int:
    problems: list[str] = []

    for h in INCLUDE.rglob("*.h"):
        rel = h.relative_to(INCLUDE)
        if len(rel.parts) == 1:
            if rel.name not in TOP_HEADERS:
                problems.append(f"include/{rel.name}: top level is the app spine only -- "
                                f"file it under {sorted(ALLOWED_DIRS)}")
            continue
        sub = rel.parts[0]
        if sub not in ALLOWED_DIRS:
            problems.append(f"include/{rel}: '{sub}/' is not part of the taxonomy")
            continue
        want = SUFFIX_RULES.get(sub)
        if want and not rel.name.endswith(want):
            problems.append(f"include/{rel}: files in {sub}/ end in {want}")
        if sub == "ops" and not OPS_OK.search(rel.name):
            problems.append(f"include/{rel}: ops files end in Ops.h or Utils.h")
        if sub == "screens" and not SCREEN_OK.search(rel.name):
            problems.append(f"include/{rel}: screens are XScreen/XDialog/XMenu, or ScreenX for "
                            f"shared screen infrastructure")
        if sub == "ecs":
            m = ECS_FORBIDDEN.search(h.read_text(encoding="utf-8"))
            if m:
                problems.append(f"include/{rel}: ecs/ is a leaf -- it includes {m.group(1)}/, "
                                f"which inverts the dependency direction")

    for c in SRC.rglob("*.cpp"):
        rel = c.relative_to(SRC)
        if len(rel.parts) == 1:
            if rel.name not in TOP_SOURCES:
                problems.append(f"src/{rel.name}: top level is main + the spine -- mirror its "
                                f"header's directory")
            continue
        sub = rel.parts[0]
        header = INCLUDE / sub / rel.name.replace(".cpp", ".h")
        if not header.exists():
            problems.append(f"src/{rel}: no mirrored header at include/{sub}/ -- src mirrors "
                            f"include exactly")

    if problems:
        print(f"check_structure: {len(problems)} violation(s)")
        for p in problems:
            print(f"  {p}")
        return 1
    print("check_structure: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
