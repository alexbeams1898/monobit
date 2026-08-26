#!/ usr / bin / env python3
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
import re
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parent.parent
PROJECT = GAME / "assets" / "maps" / "world.ldtk"


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


# WHERE A HOLE KIND LIVES, read from the code that decides it rather than written down again.
# The builder wraps the map's bare kind in a directory and an extension; saying so a second time
# here means the day that directory moves this lint goes on checking the old one and passes
# things that cannot load.
def holes_dir() -> Path:
    build = GAME / "src" / "ops" / "AreaBuildOps.cpp"
    if build.exists():
        found = re.search(r'"(config/[a-z_]+/)" \+ kind', build.read_text(encoding="utf-8"))
        if found:
            return GAME / found.group(1)
    return GAME / "config" / "holes"


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
#A HOLE WITH NO KIND IS NOT A HOLE.Unset, it names no hole file and no art, so the
#game builds a placeholder box that leaks nothing-- an authored point of entry that
#looks like it has already been cleared, on a brand new game.Loud here, because the
#game's own complaint is one line in a log nobody reads on a good day.
        for hole in entities(level, "Hole"):
            kind = field(hole, "kind")
            if not kind:
                errors.append(f"{name}: a Hole has no 'kind' -- it would build as a box that "
                              f"never leaks")
            elif "/" in kind or kind.endswith(".json"):
                # The map names the KIND; where hole files live is the code's business, and it
                # wraps the name in the path. A path here becomes config/holes/<path>.json --
                # nonsense that only shows up as one line in a log at run time.
                errors.append(f"{name}: Hole kind '{kind}' is a path -- name the kind alone, "
                              f"as '{Path(kind).stem}'")
            elif not (holes_dir() / f"{kind}.json").exists():
                errors.append(f"{name}: Hole kind '{kind}' names no file in "
                              f"{holes_dir().relative_to(GAME).as_posix()}")

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
            warps[wid] = {
    "level" : name, "target" : target}
#PlayerStart IS the game's start, so the project carries exactly one.
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
