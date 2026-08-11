#!/usr/bin/env python3
"""Map lint: the LDtk project's door contract, enforced by machine.

Warps are the only cross-level references in the map, so they are where
authoring can silently break: a renamed id leaves its partner pointing at
nothing, and the game only finds out when a player walks into the passage.

Errors (exit 1):
  - a Warp without `id`, `target`, or a valid `facing`
  - two Warps sharing an id
  - a `target` naming no warp in the project
  - more than one PlayerStart (it IS the game's start)

Warnings (exit 0):
  - a one-way passage (A targets B, but B does not target A)
  - no PlayerStart anywhere

A missing project file passes with a note -- the map simply has not been
authored yet.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent / "assets" / "maps" / "world.ldtk"


def entities(level: dict, identifier: str) -> list[dict]:
    for layer in level.get("layerInstances") or []:
        if layer.get("__identifier") == "Entities":
            return [e for e in layer.get("entityInstances") or []
                    if e.get("__identifier") == identifier]
    return []


def field(entity: dict, name: str) -> str:
    for fi in entity.get("fieldInstances") or []:
        if fi.get("__identifier") == name and isinstance(fi.get("__value"), str):
            return fi["__value"]
    return ""


def main() -> int:
    if not PROJECT.exists():
        print("check_areas: no assets/maps/world.ldtk yet -- nothing to lint")
        return 0
    try:
        project = json.loads(PROJECT.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        print(f"check_areas: cannot read {PROJECT.name}: {e}")
        return 1

    errors: list[str] = []
    warnings: list[str] = []
    warps: dict[str, dict] = {}  # id -> {level, target}
    starts: list[str] = []  # levels holding a PlayerStart (the game's one start)

    for level in project.get("levels") or []:
        name = level.get("identifier", "?")
        for _ in entities(level, "PlayerStart"):
            starts.append(name)
        for warp in entities(level, "Warp"):
            wid = field(warp, "id")
            target = field(warp, "target")
            if not wid or not target:
                errors.append(f"{name}: a Warp needs both 'id' and 'target'")
                continue
            if field(warp, "facing").lower() not in ("north", "south", "east", "west"):
                errors.append(f"{name}: warp '{wid}' needs a facing "
                              f"(north/south/east/west)")
            if wid in warps:
                errors.append(f"warp id '{wid}' declared in both "
                              f"'{warps[wid]['level']}' and '{name}'")
                continue
            warps[wid] = {"level": name, "target": target}
    # PlayerStart IS the game's start, so the project carries exactly one.
    if len(starts) > 1:
        errors.append(f"more than one PlayerStart in the project: {', '.join(starts)}")
    elif not starts:
        warnings.append("no PlayerStart anywhere -- the game falls back to a "
                        "generated floor")

    for wid, w in warps.items():
        if w["target"] not in warps:
            errors.append(f"warp '{wid}' targets '{w['target']}', which no "
                          f"level declares")
        elif warps[w["target"]]["target"] != wid:
            warnings.append(f"one-way passage: '{wid}' -> '{w['target']}', "
                            f"but not back")

    for w in warnings:
        print(f"  warning: {w}")
    if errors:
        print(f"check_areas: {len(errors)} error(s)")
        for e in errors:
            print(f"  {e}")
        return 1
    print(f"check_areas: clean ({len(warps)} warp(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
