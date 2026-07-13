#pragma once

#include "TileMap.h"

#include <string>
#include <vector>

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

// The imported region: everything a region needs to render + spawn. The tile grid
// + config fill the engine's TileMap/TileConfig; objects spawn ECS entities.
struct Region
{
    TileMap map;       // flat 32px tile grid (top tile per cell wins for v1)
    TileConfig config; // tileset atlas path + per-id uv/walkable
    std::vector<Object> objects;
    int fill_uv_col = 0; // border-fill tile (beyond the authored bounds)
    int fill_uv_row = 0;
    bool ok = false; // false if the file was missing / unparseable
};

// Load a region from an .ldtk file. `tileset_path` is the engine's 32px RENDER
// atlas (e.g. "assets/tilesets/overworld.png") -- the authoring source in the
// .ldtk is 16px, but the atlas cell indices match, so the importer just points at
// the render atlas. Returns Region{ok=false} on any failure (missing/bad file).
Region load(const std::string& ldtk_path, const std::string& tileset_path);

} // namespace ldtk
