# Wayworn Hush — Map & World Architecture

> **Owns:** how the world is structured, authored, stored, and loaded — tile
> data, per-tile behavior, collision, objects/entities, region connection, and
> the authoring tool. Grounded in research into how Pokémon Gen 3, EarthBound,
> and the SNES Final Fantasies structured their maps, then modernized for an
> ECS + data-driven-from-JSON engine.
>
> **Status:** proposal for review. Decisions marked **[LOCKED]** were confirmed
> in session; **[PROPOSED]** await sign-off before implementation.

## tldr

The world is a **hybrid open world**: discrete authored regions stitched
seamlessly at their edges (so overworld travel flows like one continuous space),
with interiors/caves reached by discrete warps. Each region's data is **four
independent structures** — a flat tile-ID grid, a tile-*definition* table where
behavior is data (not inferred from graphics), per-cell collision, and an object
layer that spawns ECS entities at load. Tiles are **bulk data, not
tile-per-entity**. Authoring tool: **LDtk** (proposed) — its schema-first typed
entities and World/Levels model fit an open world with quest/NPC data better
than Tiled. **"Quests" are the inner-monologue system, not a conventional quest
subsystem** — see §6, the most important design decision here.

---

## 1. What the research settled (the universal pattern)

Across Pokémon Gen 3, EarthBound, FF5, and FF6, the same architecture recurs.
These are now **[LOCKED]** because every reference game agrees and each matches
this engine's doctrine:

1. **Metatile indirection.** Maps don't store raw tiles; they store indices into
   NxN *blocks* (16×16px = a 2×2 of 8×8 tiles, universally). Saves memory,
   speeds authoring. Our engine's tile is already the authored unit; we adopt the
   *concept* (author in reusable blocks) without necessarily the exact hardware
   layout.
2. **Behavior is data, never inferred from graphics.** Whether a tile is
   grass/water/ledge/blocking is a *behavior value* (Pokémon's `MB_*` byte,
   FF6's solidity set, EarthBound's collision flags), stored in a table keyed by
   tile type — not read from what the tile looks like. This is the single most
   important structural lesson and it is exactly "config over constants."
3. **Four independent data structures per map** (Pokémon's cleanest form):
   - the **tile grid** (flat array of tile/block IDs),
   - the **tile-definition table** (per-tile-*type*: sprite + collision +
     behavior + footstep, keyed by ID, shared by every instance),
   - **per-cell collision/elevation** (Pokémon packs 2 collision + 4 elevation
     bits per cell for bridges/overpasses),
   - **event/object lists** (NPCs, warps, triggers) stored *apart from* the tile
     grid — nothing in a tile cell marks an NPC.
4. **Walk-behind via layers + priority.** A character passes behind a treetop
   because the tree's upper half draws on a layer above the sprite. Our engine
   already does this: RenderSystem's Y-sort (layer → foot-Y → sub_layer). We do
   **not** need to invent it — see [`engines/engine/docs/ENGINE.md`](../../../../engines/engine/docs/ENGINE.md)
   "Top-down depth sorting."

## 2. The modern ECS confirmation

Research into Tiled, LDtk, Godot, and bevy_ecs_tilemap converged on the same
engine-side model — and it's the one this engine already leans toward:

- **Tiles are bulk data, not entities.** A tilemap is a flat `std::vector` of
  tile IDs (our `TileMap.tiles` already is this). Making every tile an ECS
  entity (bevy_ecs_tilemap's approach) strains at scale; the ecosystem grew
  flat-array alternatives for exactly that reason. Per-tile *type* properties
  live in a definition table keyed by ID.
- **Only dynamic/interactive things become entities.** Objects from the map's
  object layer — NPCs, warps, triggers, pickups — are spawned as ECS entities at
  load time, dispatched by their type/class. Tiles stay bulk.
- **This matches Godot's TileSet model exactly:** cells store only IDs; behavior
  (collision, custom data) is defined per tile *type* on the shared tileset.
  "Tile behavior is a tile-definition property, not an instance property."

So the engine-side architecture is **editor-independent**: flat tile-ID chunks +
a `TileDefinition` table + an object→entity spawner. The authoring-tool choice
(§5) is about *authoring ergonomics*, not engine structure.

## 3. World model — hybrid open world **[LOCKED]**

Chosen: **discrete authored regions, stitched seamlessly at edges + discrete
warps for interiors** — Pokémon's two linking primitives, tuned so the outdoor
world reads as one continuous open space (EarthBound's *feel*, without one
monolithic world file).

- **Regions** are the authored unit (a forest, a mountain pass, a coastline).
  Each is its own map with its own four data structures (§1.3). Author and edit
  in isolation; load a region + its edge-neighbors.
- **Connections** stitch neighbors: a record `{direction, offset, target-region}`.
  At load, the neighbor's edge tiles are copied into a border buffer so the seam
  is invisible and walkable — you cross from region to region with no load screen.
  (Pokémon's `MapConnection` + border-fill, verbatim.)
- **Warps** are discrete teleports: `{x, y → target-region, warpId}` for cave
  mouths, doorways, shrine interiors. Invisible; distinct from connections.
- **Open-world traversal:** no gating; the player picks direction freely. The
  finite authored set of regions forms a connected graph. Matches DESIGN.md
  ("pick which region next," "finite and authored, not procgen") and SCALE.md
  ("authored regions, whole-region load").

**Why not one continuous EarthBound grid:** a single monolithic world file gets
unwieldy, holds all collision/behavior in memory at once, and can't be edited
region-by-region. The hybrid gets the seamless *feel* while keeping regions
independently authorable and memory-bounded. **Why not pure discrete-Pokémon
(visible load seams):** the aesthetic wants an unbroken pilgrimage; seams break
that. The stitched hybrid is the correct middle.

## 4. Region data model **[PROPOSED]**

Per region, four decoupled structures (JSON, per doctrine):

- **`tiles`** — flat row-major array of tile IDs. One or more visual layers
  (ground, decoration, overhang) for walk-behind depth.
- **`TileDefinition` table** — keyed by tile ID: `{ sprite/atlas-ref, walkable,
  behavior, footstep_bank, layer_type }`. `behavior` is an enum
  (`grass`/`water`/`ledge_south`/`path`/…) driving encounters, footsteps,
  ambient events — *never inferred from the sprite*. Shared across all regions
  (a global tile-definition set) so a tree behaves identically everywhere.
- **per-cell collision** — walkable derives from the tile definition by default;
  a per-cell override layer handles exceptions (a normally-walkable tile blocked
  here). Elevation deferred until a region actually needs bridges/overpasses.
- **object layer** — list of `{type, x, y, ...typed-fields}`. At load, each
  becomes an ECS entity: player-spawn, NPC, warp, connection-marker, trigger,
  pickup, monologue-trigger. Dispatched by `type`.

The engine's `TileMap` already provides the flat grid + `tile_size` + walkable
flag. This proposal adds: the `TileDefinition` behavior table, multi-layer
support, and the object→entity spawner. All game-side; the engine `TileMap`
stays generic.

## 5. Authoring tool — LDtk **[LOCKED]**

**LDtk** (ldtk.io), not Tiled. Both export JSON we import; the engine-side model
is identical either way. LDtk wins for *this* game because:

- **Schema-first typed entities.** An LDtk entity type ("NPC," "MonologueTrigger,"
  "Warp") defines typed fields with constraints (`Int[0,100]`, `Enum`, `Point`,
  `EntityRef`). Every instance carries exactly those validated fields. This is
  built for the object layer we need; Tiled's open property-bag is looser and
  validates only at runtime.
- **Enums + cross-level `EntityRef`.** Author a region/behavior/monologue enum
  once; reference it in fields. `EntityRef` links entities across regions
  (a warp's destination, a monologue thread's prerequisite) by stable ID — exactly
  what an open-world graph needs.
- **IntGrid layers** — paint semantic values (collision, region-behavior zones)
  directly; the flat int array imports straight into our collision/behavior layer.
- **World → Levels model** with precomputed neighbour adjacency = a clean
  streaming/connection unit for the hybrid open world (§3). Cleaner than Tiled's
  infinite-map chunk-soup.
- **`__`-denormalized JSON** — resolved def data is copied onto instances, so a
  hand-written C++ importer barely cross-references. Materially less importer code.

**Cost / risk:** LDtk's C++ ecosystem is less mature than Tiled's (we write our
own importer either way — fine), and some LDtk `worlds[]` schema fields are
mid-migration (importer reads root-or-worlds[] defensively). Tiled's only real
edges are ubiquity and TMX maturity — neither outweighs LDtk's fit here.

**A bespoke JSON format was considered and rejected:** hand-editing tile grids
in JSON doesn't scale and reinvents what LDtk gives free (visual painting,
auto-tiling, typed entities). One import step against a mature editor is the
correct "one extra tool, done right."

## 6. Quests = the inner-monologue system (NOT a quest subsystem) **[LOCKED]**

The most important design decision here. Wayworn Hush's "quests" surface as **the
protagonist's own thoughts and interpretations**, not NPC dialogue or a quest
log. A quest is a thread of *evolving interpretation* driven by world-events
(reaching a place, seeing a thing, weather/season turning) — not "NPC assigns
task → tracked objective → dialogue reward."

**Architectural consequence: the quest system and the inner-monologue system are
one system.** It is the `insight`-graph + tiered-`lang` mechanic from
[AUDIT.md](AUDIT.md) §3:

- **Flags fire from world-events** (an `insight`-style event-driven flag graph):
  entered-the-high-pass, saw-the-lone-tree, camped-by-the-river-at-dusk.
- **"Quest progress" is the monologue register deepening** — tiered text (`lang`)
  where a thread's later tiers unlock as its flags fire. The protagonist's
  interpretation shifts; that shift *is* the quest advancing.
- **No quest-log UI, no objective markers, no NPC dialogue trees.** (Matches
  AESTHETIC.md "no urgent objective markers, waypoints, quest arrows" and
  ORIGINAL_NOTES "mostly inner thoughts.")
- **NPCs remain sparse and brief** (AESTHETIC.md "one small human moment, then
  the player moves on"). An NPC encounter may *fire a flag* that seeds a
  monologue thread — but the text you read afterward is the protagonist's own,
  not a conversation.

This is smaller and more unified than a conventional RPG quest system, and it is
faithful to ORIGINAL_NOTES. It means: **do not build a quest subsystem.** Build
the monologue/insight system well (§ future work), and quests are an authoring
pattern on top of it — a named thread of flags + tiered text, authored as data.

This also settles the AUDIT's open question about lifting selva's `lang`/`insight`:
since this is now core (not a minor monologue feature), the tiered-register model
is worth the lift — `lang` is near-free, `insight` needs the profile-state
adapter. Decision to lift vs. build bespoke is deferred to the monologue-system
work, but the *shape* (event-flags → tiered interpretive text) is locked.

## 7. What this means for the vertical slice (SLICE.md step 3)

Step 3 ("one authored region renders") becomes:

1. Write the **LDtk importer** (region JSON → engine `TileMap` + `TileDefinition`
   table + object list). Start with just tile layers + one object type
   (player-spawn); grow object types as systems land.
2. Author **one small region** in LDtk (a ~48×48-tile meadow/forest edge) with a
   placeholder 32px tileset.
3. Render it through the pixel target (§ engine `TileMapRenderer`, already built).

Connections, warps, behavior-driven encounters, and the monologue/quest layer are
**later steps** — step 3 only needs a single region's tiles on screen. But the
importer and data model are built to the architecture above from line one, so
those later layers slot in without rework.

## Open questions

- **Elevation/bridges** — deferred until a region needs an overpass. The data
  model reserves the concept (Pokémon's 4-bit elevation) but v1 skips it.
- **Streaming window** — whole-region load + edge-neighbor stitch is enough at
  the region sizes in SCALE.md; a load/unload hysteresis window is a later
  optimization, not needed for the slice.
- **Global vs per-region tile-definition table** — proposed global (a tree
  behaves the same everywhere); revisit if regions need conflicting behaviors
  for the same visual.

## Cross-references

- [AUDIT.md](AUDIT.md) — engine/prison-escape reuse; `lang`/`insight` modules (§3).
- [SCALE.md](SCALE.md) — tile size (16px), region sizing, render target.
- [SLICE.md](SLICE.md) — the build plan step 3 refines.
- [DESIGN.md](DESIGN.md) / [AESTHETIC.md](AESTHETIC.md) / [ORIGINAL_NOTES.md](ORIGINAL_NOTES.md)
  — the register §6 serves.
