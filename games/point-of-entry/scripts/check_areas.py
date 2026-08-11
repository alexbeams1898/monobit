#!/usr/bin/env python3
"""Map lint: the LDtk project's door contract, enforced by machine.

Doors are the only cross-level references in the map, so they are where
authoring can silently break: a renamed id leaves its partner pointing at
nothing, and the game only finds out when a player walks into the doorway.

Errors (exit 1):
  - a Door without both `id` and `target`
  - two Doors sharing an id
  - a `target` naming no door in the project

Warnings (exit 0):
  - a one-way passage (A targets B, but B does not target A)
  - a level with no PlayerStart (doorless entry lands wherever he stood)

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
    doors: dict[str, dict] = {}  # id -> {level, target}

    for level in project.get("levels") or []:
        name = level.get("identifier", "?")
        for door in entities(level, "Door"):
            did = field(door, "id")
            target = field(door, "target")
            if not did or not target:
                errors.append(f"{name}: a Door needs both 'id' and 'target'")
                continue
            if did in doors:
                errors.append(f"door id '{did}' declared in both "
                              f"'{doors[did]['level']}' and '{name}'")
                continue
            doors[did] = {"level": name, "target": target}
        if not entities(level, "PlayerStart"):
            warnings.append(f"{name}: no PlayerStart -- doorless entry lands "
                            f"wherever he already stood")

    for did, d in doors.items():
        if d["target"] not in doors:
            errors.append(f"door '{did}' targets '{d['target']}', which no "
                          f"level declares")
        elif doors[d["target"]]["target"] != did:
            warnings.append(f"one-way passage: '{did}' -> '{d['target']}', "
                            f"but not back")

    for w in warnings:
        print(f"  warning: {w}")
    if errors:
        print(f"check_areas: {len(errors)} error(s)")
        for e in errors:
            print(f"  {e}")
        return 1
    print(f"check_areas: clean ({len(doors)} door(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
