#!/usr/bin/env python3
"""Map linter for the wayworn-hush LDtk project.

Checks the authored map against the rules the importer and the warp system
actually live by (docs/design/MAP-PIPELINE.md), so authoring slips surface here
instead of as silent in-game weirdness:

  errors (exit 1):
    - a Warp with no target_level, or one naming a level that does not exist
    - fully-opaque tiles buried under other fully-opaque tiles (invisible
      garbage from painting over; --fix-stacks deletes them)
  warnings:
    - a Warp with no facing (defaults to south -- fine for south doors, a trap
      for stairs)
    - a one-way passage (no warp in the target level points back) and
      ambiguous return-pairs (several point back, none disambiguated by ids)
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


def check_warps(levels, warps):
    for lvl_id, f, _e in warps:
        target = f.get("target_level")
        if not target:
            errors.append(f"{lvl_id}: Warp with no target_level (importer drops it)")
        elif target not in levels:
            errors.append(f"{lvl_id}: Warp targets '{target}' which does not exist")
        if not f.get("facing"):
            warnings.append(f"{lvl_id}: Warp to '{target}' has no facing (defaults to south)")

    # Return-pairing per (level -> target): a passage should have a way back, and an
    # unambiguous one unless ids disambiguate.
    back = defaultdict(list)
    for lvl_id, f, _e in warps:
        if f.get("target_level") in levels:
            back[(lvl_id, f["target_level"])].append(f)
    for (src, dst), fs in back.items():
        if (dst, src) not in back:
            warnings.append(f"{src} -> {dst}: no warp back (arrival falls to default spawn)")
        returns = back.get((dst, src), [])
        if len(returns) > 1 and not all(f.get("target") for f in fs):
            warnings.append(
                f"{src} -> {dst}: {len(returns)} return warps in '{dst}' but not every "
                f"'{src}' warp names a target -- auto-pair will guess")


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
    check_warps(levels, warps)
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
