#!/usr/bin/env python3
"""Map linter for the wayworn-hush LDtk project.

Checks the authored map against the rules the importer and the warp system
actually live by (docs/design/MAP-PIPELINE.md), so authoring slips surface here
instead of as silent in-game weirdness:

  errors (exit 1):
    - a Warp with no id or no target_id, a target that names no warp/spawn, a
      duplicate warp id, a target in the warp's OWN level, or a pair that does
      not name each other (a passage is mutual)
    - fully-opaque tiles buried under other fully-opaque tiles (invisible
      garbage from painting over; --fix-stacks deletes them)
  warnings:
    - a Warp with no facing (defaults to south -- fine for south doors, a trap
      for stairs)
    - a target that names no way back (a one-way passage)
    - fill_tile referencing a different tileset than the level's ground
    - a level painted with an interior tileset but interior != true
    - a ground tileset with no tag-source enum (nothing can be tagged Wall)
    - level identifiers off the naming convention (PascalCase, optional
      _F<n>/_B<n> floor suffix)
    - not exactly one id-less PlayerSpawn (the new-game start) project-wide

Run from anywhere; paths resolve relative to the game directory.
"""
import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

GAME_DIR = Path(__file__).resolve().parent.parent
LEVEL_NAME = re.compile(r"^[A-Z][A-Za-z0-9]*(_[FB][0-9]+)?$")

errors = []
warnings = []


def entity_fields(e):
    return {fi["__identifier"]: fi.get("__value") for fi in e.get("fieldInstances", [])}


def ground_layer(level):
    for li in level.get("layerInstances", []):
        if li.get("__identifier") == "Ground":
            return li
    return None


def collect(j):
    """(levels-by-id, warps as (level_id, fields, entity), spawns as (level_id, fields))."""
    levels, warps, spawns = {}, [], []
    for lvl in j["levels"]:
        levels[lvl["identifier"]] = lvl
        for li in lvl.get("layerInstances", []):
            for e in li.get("entityInstances", []):
                if e["__identifier"] == "Warp":
                    warps.append((lvl["identifier"], entity_fields(e), e))
                elif e["__identifier"] == "PlayerSpawn":
                    spawns.append((lvl["identifier"], entity_fields(e)))
    return levels, warps, spawns


def check_warps(levels, warps, spawns):
    """A door names the DOOR it arrives at (`target_id`), and nothing else -- the
    level falls out of the lookup, so the map never states a destination twice and
    the two halves can never disagree. Every door must therefore: have an id, name
    a target that exists, be named back by it (a passage is mutual), sit in a
    different level than its target, and carry a facing (the way you step out)."""
    by_id = {}
    for lvl_id, f, _e in warps:
        wid = f.get("id")
        if not wid:
            errors.append(f"{lvl_id}: a Warp has no id (nothing can target it)")
            continue
        if wid in by_id:
            errors.append(f"warp id '{wid}' is used in both '{by_id[wid][0]}' and "
                          f"'{lvl_id}' -- ids must be unique")
            continue
        by_id[wid] = (lvl_id, f)
    # A named spawn is a legal arrival too (the map's start is the id-less one).
    for lvl_id, f in spawns:
        if f.get("id"):
            by_id.setdefault(f["id"], (lvl_id, f))

    for lvl_id, f, _e in warps:
        wid, target = f.get("id"), f.get("target_id")
        if not wid:
            continue
        if not target:
            errors.append(f"{lvl_id}/{wid}: no target_id (the importer drops it)")
            continue
        if target not in by_id:
            errors.append(f"{lvl_id}/{wid}: targets '{target}', which is no warp or spawn "
                          f"anywhere in the map")
            continue
        dst_level, dst = by_id[target]
        if dst_level == lvl_id:
            errors.append(f"{lvl_id}/{wid}: targets '{target}' in its OWN level")
        back = dst.get("target_id")
        if back and back != wid:
            errors.append(f"{lvl_id}/{wid}: targets '{target}', but '{target}' targets "
                          f"'{back}' -- a passage names itself on both sides")
        elif not back:
            warnings.append(f"{lvl_id}/{wid}: targets '{target}', which names no way back")
        if not f.get("facing"):
            warnings.append(f"{lvl_id}/{wid}: no facing (defaults to south)")


def check_levels(j, levels):
    overworld_uids = {ts["uid"] for ts in j["defs"]["tilesets"]
                      if "overworld" in str(ts.get("relPath", "")).lower()}
    tilesets = {ts["uid"]: ts for ts in j["defs"]["tilesets"]}
    for lvl_id, lvl in levels.items():
        if not LEVEL_NAME.match(lvl_id):
            warnings.append(f"{lvl_id}: identifier off convention (PascalCase, _F<n>/_B<n>)")
        g = ground_layer(lvl)
        if g is None:
            errors.append(f"{lvl_id}: no Ground layer")
            continue
        uid = g.get("__tilesetDefUid", -1)
        fields = {fi["__identifier"]: fi.get("__value")
                  for fi in lvl.get("fieldInstances", [])}
        fill = fields.get("fill_tile")
        if fill and uid >= 0 and fill.get("tilesetUid", -1) not in (-1, uid):
            warnings.append(f"{lvl_id}: fill_tile references tileset "
                            f"{fill.get('tilesetUid')} but the ground is painted with {uid}")
        if uid >= 0 and uid not in overworld_uids and fields.get("interior") is not True:
            warnings.append(f"{lvl_id}: painted with an interior tileset but interior != true")
        ts = tilesets.get(uid)
        if ts is not None and ts.get("tagsSourceEnumUid") is None:
            warnings.append(f"{lvl_id}: ground tileset '{ts.get('identifier')}' has no "
                            f"tag-source enum -- nothing can be tagged Wall/Water")


def check_spawns(spawns):
    # The id-less PlayerSpawn IS where a new game starts (the game derives the start
    # level from it -- no config twin), so there must be exactly one.
    default = [lvl for lvl, f in spawns if not f.get("id")]
    if len(default) != 1:
        warnings.append(f"expected exactly one id-less PlayerSpawn (the new-game start), "
                        f"found {len(default)}: {default}")


def load_sheets(j):
    """tileset uid -> (RGBA image, grid size); None if Pillow is unavailable."""
    try:
        from PIL import Image
    except ImportError:
        return None
    sheets = {}
    for ts in j["defs"]["tilesets"]:
        rel = ts.get("relPath")
        if not rel:
            continue
        p = GAME_DIR / "assets" / "tilesets" / "source" / Path(rel).name
        if p.exists():
            sheets[ts["uid"]] = (Image.open(p).convert("RGBA"), ts["tileGridSize"])
    return sheets


def check_stacks(j, levels, fix, ldtk_path):
    sheets = load_sheets(j)
    if sheets is None:
        warnings.append("Pillow not installed -- buried-tile check skipped")
        return
    opaque_cache = {}

    def fully_opaque(uid, src):
        key = (uid, tuple(src))
        if key not in opaque_cache:
            img, gs = sheets[uid]
            tile = img.crop((src[0], src[1], src[0] + gs, src[1] + gs))
            opaque_cache[key] = min(px[3] for px in tile.getdata()) == 255
        return opaque_cache[key]

    dirty = False
    for lvl_id, lvl in levels.items():
        g = ground_layer(lvl)
        if g is None or g.get("__tilesetDefUid") not in sheets:
            continue
        uid = g["__tilesetDefUid"]
        cells = defaultdict(list)
        for t in g.get("gridTiles", []):
            cells[tuple(t["px"])].append(t)
        keep_ids, removed = set(), 0
        for stack in cells.values():
            keep = []
            for t in reversed(stack):  # top of the stack first
                keep.append(t)
                if fully_opaque(uid, t["src"]):
                    break
            removed += len(stack) - len(keep)
            keep_ids.update(id(t) for t in keep)
        if removed:
            if fix:
                g["gridTiles"] = [t for t in g["gridTiles"] if id(t) in keep_ids]
                dirty = True
                print(f"[fix] {lvl_id}: removed {removed} buried tiles")
            else:
                errors.append(f"{lvl_id}: {removed} tiles buried under fully-opaque "
                              f"tiles (run with --fix-stacks)")
    if dirty:
        ldtk_path.write_text(json.dumps(j, indent=1, ensure_ascii=False) + "\n",
                             encoding="utf-8", newline="\n")
        print(f"[fix] wrote {ldtk_path} -- reload the project in the editor")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fix-stacks", action="store_true",
                    help="delete tiles buried under fully-opaque tiles")
    args = ap.parse_args()

    ldtk_path = GAME_DIR / json.loads(
        (GAME_DIR / "config" / "world.json").read_text(encoding="utf-8"))["ldtk"]
    j = json.loads(ldtk_path.read_text(encoding="utf-8"))

    levels, warps, spawns = collect(j)
    check_warps(levels, warps, spawns)
    check_levels(j, levels)
    check_spawns(spawns)
    check_stacks(j, levels, args.fix_stacks, ldtk_path)

    for w in warnings:
        print(f"[ldtk-lint] WARNING: {w}")
    for e in errors:
        print(f"[ldtk-lint] ERROR: {e}")
    print(f"[ldtk-lint] {len(levels)} levels, {len(warps)} warps, {len(spawns)} spawns: "
          f"{len(errors)} errors, {len(warnings)} warnings")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
