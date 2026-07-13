# Wayworn Hush — Map Pipeline (tileset + LDtk importer)

> **Owns:** the concrete pipeline that turns CC0 source art + an LDtk-authored
> level into the engine's live map — the pieces MAP-ARCHITECTURE.md's data model
> left as "write the importer / author a region." Design-first on the NEW
> mechanics only; the region data model, world model, and LDtk-as-editor are
> already **[LOCKED]** in [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md).
>
> **Status:** design proposal, pre-build.

## LDtk is external (not embeddable) — settled

LDtk's editor **cannot be embedded** in the game/engine — it's a standalone
desktop app. You author in it and alt-tab; we import the `.ldtk` JSON. That is
accepted (a mature editor for free vs. building/maintaining our own). LDtk does
publish loader libraries (incl. a C++ one, `Madour/LDtkLoader`); we may use it or
write a thin parser — the format is denormalized JSON either way (Piece 2).

## What already exists (don't rebuild)

- **Engine tile rendering is atlas-ready.** `TileConfig` (engine `TileMap.h`)
  already has `tileset_path` (one atlas PNG), `atlas_tile_size`, and per-tile-id
  `tile_visuals{uv_col, uv_row, r,g,b}`. When `tileset_path` is non-empty,
  `TileMapRenderer` draws **textured quads** from the atlas cell `(uv_col,uv_row)`;
  empty → the flat-color fallback we use today. So the runtime needs **no change**
  to show real tiles — only the data (atlas + per-id UVs + walkable) must be filled.
- **The data model is locked** (MAP-ARCHITECTURE §4): flat tile-ID grid +
  `TileDefinition` behavior table + per-cell collision + object layer. LDtk is the
  authoring tool (§5). This doc specifies the two build artifacts that realize it.

## The source art

ArMM1998 "Zelda-like tilesets and sprites" (**CC0**, no attribution required).
`gfx/Overworld.png` (640×576, 40×36 @16px) = terrain + decoration; `gfx/objects.png`
= props; `character.png`, `cave.png`, `Inner.png` = later. Source tiles are **16px**;
our world grid is **32px** (SCALE.md, tied to the 32×64 protagonist). Reconciled by
**×2 nearest-neighbor** (integer, lossless-crisp) — the art reads chunkier, which
leans *more* EarthBound and matches the protagonist's pixel density.

## The source art is autotile-structured (why LDtk owns the tiling)

The ArMM1998 sheet is not a flat palette of interchangeable tiles — it is laid out
as **3×3 (9-slice) edged terrain blocks** (a grass region's corners/edges/center;
a water body's shoreline pieces) plus **multi-tile props** (a rock is 2×2, a house
4×4). You don't pick "the water tile"; you paint a water *region* and the correct
edge/corner pieces fill in.

**That is exactly LDtk's auto-layer / rule-based tiling.** So the edge logic lives
in **LDtk**, defined once as autotile rules against the tileset — NOT curated
cell-by-cell in a slicer pick list (the earlier plan, revised once the sheet's
structure was understood). This keeps us from reimplementing autotiling the engine
would otherwise have to own.

## Piece 1 — the tileset scaler (a build tool)

`tools/tileset/slice.py` (Pillow) — deliberately minimal now that LDtk owns the
tiling. It scales the **whole** source sheet, no per-cell curation:

- **Input:** a source sheet (clean 16px grid), e.g. `source/Overworld.png`.
- **Transform:** **×2 nearest → 32px** (integer/crisp; leans EarthBound + matches
  the 32px-density protagonist) → `rgb555_snap` (SPRITE-PIPELINE's hardware-color
  grid, for register consistency).
- **Output:** `assets/tilesets/overworld.png` (+ `objects.png`) — the 32px tileset
  on the same clean grid the source had, ready to drop into LDtk as a tileset def.

No pick list, no per-tile manifest here. **Behavior** (walkable / behavior /
footstep) is authored where it belongs: an LDtk **IntGrid** collision/behavior
layer + the engine's global tile-definition table — never inferred from the sprite
(MAP-ARCHITECTURE §1.2, §4).

## Piece 2 — the LDtk importer (runtime)

`ldtk::load(region_path) → fills engine TileMap + TileConfig + an object list`.
LDtk exports denormalized JSON (`__`-fields), so the importer barely
cross-references (MAP-ARCHITECTURE §5).

- **Input:** a region `.ldtk` (or per-level `.ldtkl`) authored against the scaled
  tileset. LDtk's autotile rules run at author time and **bake** the resolved tile
  placements into the export — each grid cell already carries the exact tileset
  source pixel `(src_x, src_y)` it should draw. The importer consumes that baked
  result; it does not run autotile logic.
- **Maps to the engine:**
  - LDtk auto/tile layer → `TileMap.tiles`: derive a stable tile ID from each
    cell's tileset `(src_x, src_y)` (i.e. `uv_col = src_x/32`, `uv_row = src_y/32`).
    Populate `TileConfig.tile_visuals[id] = {uv_col, uv_row}`, set
    `TileConfig.tileset_path = assets/tilesets/overworld.png`, `atlas_tile_size=32`.
  - LDtk **IntGrid** (collision/behavior) layer → per-cell walkable + the tile
    definition's behavior (grass/water/path/blocking), independent of the visual
    (MAP-ARCHITECTURE §1.2, §4). This is the source of truth for `walkable`, not
    the sprite.
  - LDtk entity layer → an **object list** `{type, x, y, fields}`; each spawns an
    ECS entity at load (player-spawn first; NPC/warp/trigger/pickup/monologue as
    those systems land — §4/§7).
- **Replaces** `buildPlaceholderRegion` as the region source once a real region
  exists; the placeholder stays as the fallback/test region.

## Layers + walk-behind (deferred to a props sub-step)

Trees/houses draw their upper half **above** the character (MAP-ARCHITECTURE §1.4).
The engine's Y-sort already does depth; multi-tile props need either an overhang
tile layer (drawn after sprites) or prop **objects** with a sort anchor. First
region = **ground tiles only** (prove the atlas + importer); props land next.

## Build order

1. **This doc** → align.
2. **Scaler** (`tools/tileset/slice.py`) → `assets/tilesets/overworld.png` +
   `objects.png` (whole sheet ×2 → 32px, rgb555-snapped). **Done.** Unit-test the
   pure scale/snap math.
3. **Author in LDtk:** import `overworld.png` as a 32px tileset, define autotile
   rules for the terrains you use (grass fill, water+shore, cliffs), lay a small
   region + a player-spawn entity. (Alex, in the LDtk app.)
4. **LDtk importer** (auto/tile layer + IntGrid + player-spawn) → `TileMap` /
   `TileConfig`. Unit-test against a tiny committed `.ldtk` fixture (pure
   JSON→struct, no GL).
5. **Render** the imported region through `TileMapRenderer`.
6. **Props sub-step:** `objects.png` prop stamps + the object/overhang layer.

## Cross-references

- [MAP-ARCHITECTURE.md](MAP-ARCHITECTURE.md) — the LOCKED data model + LDtk choice.
- [SPRITE-PIPELINE.md](SPRITE-PIPELINE.md) — `rgb555_snap` reused for register.
- [SCALE.md](SCALE.md) — 32px grid, ×2 rationale.
- Engine `TileConfig`/`TileMapRenderer` — the atlas interface this fills.
