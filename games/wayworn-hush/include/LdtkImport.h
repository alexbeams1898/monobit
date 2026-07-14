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
// LDtk entity identifier ("PlayerSpawn", later NPC/Warp/...); (wx,wy) is its
// top-left in WORLD pixels (32px grid). Typed fields come later as systems land.
struct Object
{
    std::string type;
    float wx = 0.0f;
    float wy = 0.0f;
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
    float wx = 0.0f; // sprite CENTER in world pixels (32px scale)
    float wy = 0.0f;
    float sort_wy = 0.0f; // Y-sort key = the prop's BASE (pivot) world-Y
    int sx = 0, sy = 0;   // atlas source rect (pixels, in the 32px render atlas)
    int sw = 0, sh = 0;

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
    TileMap map;       // flat 32px tile grid (top tile per cell wins for v1)
    TileConfig config; // tileset atlas path + per-id uv/walkable
    std::vector<Object> objects;
    std::vector<Prop> props; // tile-carrying entities -> Y-sorted sprite + collider
    int fill_uv_col = 0;     // border-fill tile (beyond the authored bounds)
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

// Load a region from an .ldtk file. `tileset_path` is the engine's 32px RENDER
// atlas (e.g. "assets/tilesets/overworld.png") -- the authoring source in the
// .ldtk is 16px, but the atlas cell indices match, so the importer just points at
// the render atlas. `surfaces` resolves each ground tile's surface tag to walkability
// (terrain collision -- no hand-painted layer). `structures` tiles resizable structure
// entities (bridges, docks) across their rect (9-slice deck + walkable + surface).
// Region{ok=false} on failure.
Region load(const std::string& ldtk_path, const std::string& tileset_path,
            const surfaces::Config& surfaces, const structures::Config& structures);

// Spawn the region's props (tile-carrying LDtk entities -- trees, rocks) as ONE
// Y-sorted sprite each, sorted by its base world-Y, so the engine's depth-sort draws
// the player in front/behind by position. Call once after load, before the loop.
void spawnProps(EntityManager& em, const Region& region, const std::string& tileset_path);

} // namespace ldtk
