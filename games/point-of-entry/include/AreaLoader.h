#pragma once

#include "TileMap.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

class EntityManager;

// Loads authored areas from the game's LDtk project (config/world.ldtk) into
// the engine's map structures. One project file; LEVELS are areas. Authoring
// happens in LDtk at the world's own 32px grid; this is a pure JSON -> data
// importer -- no GL, unit-testable. (The reference implementation for the
// pattern is wayworn-hush's importer; this is its minimal cousin.)
//
// EVERY entity placed in a level becomes a typed object: the LDtk identifier
// (PascalCase) lowers to the builder key (snake_case, Door -> door), entity
// fields land in `props` verbatim. Adding a capability to the world is a new
// entity definition in LDtk plus a registered builder here -- the importer
// never changes.
//
// Walkability is the tileset's: atlas cells tagged with the Solid enum block;
// everything else walks. No painted collision layer to drift from the art.
namespace area
{

// One placed thing. (x,y) is its centre in world pixels; (w,h) its authored
// size. `props` holds the entity's fields by their LDtk identifiers.
struct Object
{
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    nlohmann::json props;
};

struct Data
{
    std::string name; // the LDtk level identifier
    int tile_size = 32;
    int width = 0;
    int height = 0;
    std::vector<TileMap::Tile> tiles;       // row-major ground; first tile per cell
    std::vector<TileMap::Decor> decoration; // stacked paint, in paint order
    TileConfig config;                      // tileset atlas + per-id uv
    std::vector<Object> objects;
    bool ok = false;
};

// Load one level by identifier; empty loads the project's first. A named
// level that does not exist fails (ok=false) rather than loading the wrong
// place silently.
Data loadLevel(const std::string& ldtkPath, const std::string& level = {});

// Every level identifier in the project, in file order. Empty on a missing
// or unparseable file.
std::vector<std::string> levels(const std::string& ldtkPath);

// The level holding THE PlayerStart -- there is exactly one in the project,
// and it is where a new game wakes. One authored fact, in the map, no config
// twin to drift. Empty when no level has one (the caller falls back to the
// generated floor).
std::string startLevel(const std::string& ldtkPath);

// The registry: build() hands each object to the builder registered for its
// type. Unknown types are logged errors -- an authored thing silently not
// existing is the failure mode this loader exists to prevent.
using Builder = std::function<void(EntityManager&, const Object&)>;
void registerBuilder(const std::string& type, Builder fn);

// Write the ground + tile config into `em` and run every object through the
// registry. GL-free: the caller uploads to the renderer.
bool build(EntityManager& em, const Data& d);

} // namespace area
