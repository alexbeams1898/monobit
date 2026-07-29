#pragma once

#include "Structures.h"
#include "Surfaces.h"
#include "TileMap.h"

#include <string>
#include <unordered_map>
#include <vector>

class EntityManager;

// Imports an LDtk region (.ldtk) into the engine's map structures. Authoring is
// done in LDtk against the NATIVE 16px source tileset; this importer applies the
// x2 to the game's 32px world grid (the scale lives in one place -- see
// docs/design/MAP-PIPELINE.md). Pure JSON -> data; no GL, unit-testable.
namespace ldtk
{

// An object from LDtk's entity layer, spawned into the ECS at load. `type` is the
// LDtk entity identifier (e.g. a future NPC); (wx,wy) is its top-left in WORLD
// pixels (32px grid). PlayerSpawn and Warp are typed out into their own structs
// below rather than travelling as bare objects.
struct Object
{
    std::string type;
    float wx = 0.0f;
    float wy = 0.0f;
};

// A named arrival point (a PlayerSpawn entity). `id` empty = where a NEW game
// starts; a warp arrives at the spawn whose id it names. `facing` is the cardinal
// the player faces on arrival ("south" when absent -- walking out of a door you
// face away from it).
struct SpawnPoint
{
    std::string id;
    float wx = 0.0f;
    float wy = 0.0f;
    std::string facing; // "north" | "south" | "east" | "west" (empty = south)
};

// A passage's end: a thin directional threshold strip laid across a doorway. It fires
// when the player's intended path crosses it while pushing AGAINST `facing` (see
// GameLoop's warp test), and it is where the matching warp in the other level arrives
// -- you emerge at the strip's center and step out along `facing`. A doorway is two
// warps whose `target_level`s point at each other; arrival auto-pairs by that return
// address, so `id`/`target` are only needed when several passages join the same two
// levels. Standing on a strip never re-fires it (the latch re-arms off-strip).
struct WarpPlacement
{
    std::string id; // this side's name -- what the other side's `target` names
    float x = 0.0f; // box center, world px
    float y = 0.0f;
    float w = 32.0f; // box size, world px
    float h = 32.0f;
    std::string target_level; // LDtk level identifier to load
    // Where to arrive: a Warp id in the target level first, else a SpawnPoint id,
    // else auto-pair by return address, else the level's default spawn.
    std::string target;
    std::string facing; // the cardinal you step out with when arriving HERE (empty = south)
};

// Where a character stands, read from an Npc entity: `npc` names the authored
// character (config/npcs/<npc>.json -- WHO), (wx,wy) is the spot they stand on
// (the entity's pivot point, world px), `facing` the way they face. An Npc entity
// may also carry an `encounter` field -- then the same placement registers as an
// encounter box (talking is observing), collected exactly like any Encounter.
struct NpcPlacement
{
    std::string npc;
    float wx = 0.0f;
    float wy = 0.0f;
    std::string facing; // "north" | "south" | "east" | "west" (empty = south)
};

// Where an observation lives in the world, read from an Encounter box on the
// Encounters layer (observability is its own concern -- physical entities carry no such
// field). The box AABB (center + size) marks the spot; you interact when within
// interact_reach of it. `trigger` is its trigger field. Kept as a neutral struct (a
// trigger STRING, not the observations enum) so the importer takes no dependency on the
// observation system -- the game maps it to psyche::Placement at load.
struct EncounterPlacement
{
    // WHICH placed thing this is -- stable across edits, unique among placements, and the
    // handle the game records permanent world changes against (a spot consumed, a thing
    // taken). Distinct from `id`: that says WHAT is here, and two placements may share it.
    std::string placement_id;
    std::string id; // the observation id this placement locates
    float x = 0.0f; // box center, world px
    float y = 0.0f;
    float w = 32.0f; // box size, world px
    float h = 32.0f;
    std::string trigger; // "Observe" | "Enter" (empty = Observe)
};

// Something collectible lying in the world, read from an entity on the Pickups layer.
// Either a static drop (a Pickup entity with an `item` field -> one item id) or a gather
// node (a Gather entity with a `loot` field -> a loot table id). (cx,cy) is its center in
// world px -- a point, not a resizable area (an item/node sits at one spot). Neutral struct
// (a target STRING + which kind) so the importer takes no dependency on inventory/loot; the
// game binds `target` to the item or loot registry at load.
struct PickupPlacement
{
    enum class Kind
    {
        Item, // `target` is an item id (a static drop)
        Loot  // `target` is a loot table id (a gather node)
    };
    // WHICH placed thing this is -- see EncounterPlacement::placement_id. A pickup that's
    // been taken is remembered by this, so it doesn't come back on the next visit.
    std::string placement_id;
    Kind kind = Kind::Item;
    std::string target; // item id (Item) or loot table id (Loot)
    float cx = 0.0f;    // center, world px
    float cy = 0.0f;
};

// A prop: an LDtk ENTITY that carries a tileset region (a tree, a rock -- placed,
// not painted). Spawned as ONE sprite covering the whole region, Y-sorted by its
// BASE (the pivot's world-Y), so the player draws in front when below the prop's
// feet and behind when above -- exactly like the player is one sprite sorted by its
// feet. This is the standard model; it has no per-tile straddle artifacts because
// the prop is a single sprite. (sx,sy,sw,sh) is the atlas source rect in pixels.
//
// Collision is DERIVED from the sprite's opaque pixels: the importer alpha-scans the
// source rect and stores the tight bounding box of the opaque region (the trunk) in
// WORLD pixels, centered form (col_cx,col_cy is the box center, col_w/col_h its size).
// So the collider hugs the trunk -- the player never bumps the transparent margin
// around a tree. col_solid is false when the sprite has no opaque pixels (no collider
// is spawned then).
struct Prop
{
    // Which plane the thing occupies -- authored as a `plane` field on the entity
    // (absent = Standing). A 2D top-down world has three honest kinds of object:
    //   Standing -- upright (house, tree): Y-sorted by its base, walk behind or in
    //               front, footprint collider at its feet.
    //   Floor    -- flat underfoot (rug, doormat): always drawn UNDER characters,
    //               no collider -- you walk on it.
    //   Cover    -- flat but enclosing (a bed): always drawn OVER characters (a
    //               body inside is under the covers, the head pokes out above the
    //               sprite), collider only at its TOP band (the raised back).
    enum class Plane
    {
        Standing,
        Floor,
        Cover
    };
    Plane plane = Plane::Standing;

    float wx = 0.0f; // sprite CENTER in world pixels (32px scale)
    float wy = 0.0f;
    float sort_wy = 0.0f; // Y-sort key = the prop's BASE (pivot) world-Y
    int sx = 0, sy = 0;   // atlas source rect (pixels, in the 32px render atlas)
    int sw = 0, sh = 0;
    // The render atlas this prop draws from -- resolved from ITS OWN tile's
    // tileset, which may differ from the sheet the level is painted with
    // (furniture in an Inner-painted room). Empty = the level's atlas.
    std::string texture_path;

    bool col_solid = false; // false = fully transparent sprite -> no collider spawned
    float col_cx = 0.0f;    // trunk AABB center + size, world pixels (opaque bounds)
    float col_cy = 0.0f;
    float col_w = 0.0f;
    float col_h = 0.0f;
};

// The imported region: everything a region needs to render + spawn. The tile grid
// + config fill the engine's TileMap/TileConfig; objects + props spawn ECS entities.
struct Region
{
    std::string level_id; // the LDtk level identifier this region came from
    TileMap map;          // flat 32px tile grid (first tile per cell = base; rest stack)
    TileConfig config;    // tileset atlas path + per-id uv/walkable
    std::vector<Object> objects;
    std::vector<SpawnPoint> spawns; // named arrival points (PlayerSpawn entities)
    std::vector<WarpPlacement> warps;
    std::vector<NpcPlacement> npcs; // characters standing in this level
    // Per-level properties (LDtk level fields). Parsed now, consumed as the systems land:
    // `music` names the level's ambient track; `interior` marks an inside space (light,
    // sound, and the camera's void treatment differ indoors).
    std::string music;
    bool interior = false;
    std::vector<Prop> props;                    // tile-carrying entities -> Y-sorted sprite
    std::vector<EncounterPlacement> encounters; // entities carrying an `encounter` field
    std::vector<PickupPlacement> pickups;       // Pickup/Gather entities on the Pickups layer
    int fill_uv_col = 0;                        // border-fill tile (beyond the bounds)
    int fill_uv_row = 0;
    // Tile id -> surface name (from the tileset's surface tags). The runtime maps the
    // tile under the player to its surface for footsteps; a tile absent here is
    // untagged (the default surface). Same tag that drives walkability at load.
    std::unordered_map<int, std::string> tile_surface;
    // Per-CELL surface override (cell index -> surface), for structures whose deck sits
    // on a different terrain tile than it sounds like (a bridge over water sounds like
    // wood). Checked before tile_surface. Sparse: only structure footing cells.
    std::unordered_map<std::size_t, std::string> cell_surface;
    bool ok = false; // false if the file was missing / unparseable
};

// A facing name ("north"|"south"|"east"|"west") -> unit direction. Empty/unknown =
// south -- walking out of a door you face away from it, and south is this world's
// "out of the door". The one mapping every consumer of an authored `facing` shares.
inline void facingVec(const std::string& facing, float& dx, float& dy)
{
    dx = 0.0f;
    dy = 1.0f;
    if (facing == "north")
        dy = -1.0f;
    else if (facing == "east")
    {
        dx = 1.0f;
        dy = 0.0f;
    }
    else if (facing == "west")
    {
        dx = -1.0f;
        dy = 0.0f;
    }
}

// The level a NEW walk begins in: the one holding the project's id-less
// PlayerSpawn (the default spawn IS the start -- one authored fact, in the map,
// no config twin to drift). Empty if no level has one (caller falls back to the
// project's first level). The map linter enforces exactly one project-wide.
std::string findStartLevel(const std::string& ldtk_path);

// Load ONE level of an .ldtk project as a region. `level` is the LDtk level
// identifier; empty loads the project's first level (the single-level case and the
// fallback when no start is configured). A named level that doesn't exist fails
// (ok=false) rather than silently loading the wrong place. `tileset_path` is the
// engine's 32px RENDER atlas (e.g. "assets/tilesets/overworld.png") -- the authoring
// source in the .ldtk is 16px, but the atlas cell indices match, so the importer just
// points at the render atlas. `surfaces` resolves each ground tile's surface tag to
// walkability (terrain collision -- no hand-painted layer). `structures` tiles
// resizable structure entities (bridges, docks) across their rect (9-slice deck +
// walkable + surface). Region{ok=false} on failure.
Region load(const std::string& ldtk_path, const std::string& tileset_path,
            const surfaces::Config& surfaces, const structures::Config& structures,
            const std::string& level = {});

// Spawn the region's props (tile-carrying LDtk entities -- trees, rocks) as ONE
// Y-sorted sprite each, sorted by its base world-Y, so the engine's depth-sort draws
// the player in front/behind by position. Call once after load, before the loop.
void spawnProps(EntityManager& em, const Region& region);

} // namespace ldtk
