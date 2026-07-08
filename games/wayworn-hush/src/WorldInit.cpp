#include "WorldInit.h"

#include "ecs/EntityManager.h"

namespace world_init
{
namespace
{
// Placeholder tile IDs. Real tile definitions arrive with the LDtk importer.
constexpr int kGrass = 0; // walkable interior
constexpr int kRock = 1;  // solid

constexpr int kRegionW = 48;
constexpr int kRegionH = 48;
} // namespace

void buildPlaceholderRegion(EntityManager& em)
{
    TileMap& map = em.tile_map;
    map.tile_size = 32;
    map.width = kRegionW;
    map.height = kRegionH;
    map.tiles.assign(static_cast<std::size_t>(kRegionW) * static_cast<std::size_t>(kRegionH),
                     TileMap::Tile{kGrass, true});

    // Solid border ring; a couple of interior rocks so depth/collision have
    // something to read against once the player exists.
    auto setTile = [&](int c, int r, int id, bool walkable)
    { map.at(c, r) = TileMap::Tile{id, walkable}; };

    for (int r = 0; r < kRegionH; ++r)
        for (int c = 0; c < kRegionW; ++c)
            if (r == 0 || c == 0 || r == kRegionH - 1 || c == kRegionW - 1)
                setTile(c, r, kRock, false);

    setTile(14, 12, kRock, false);
    setTile(30, 20, kRock, false);
    setTile(20, 32, kRock, false);

    // Flat-color visuals (no atlas yet). Muted greens/greys per the aesthetic
    // placeholder register -- superseded by an authored tileset later.
    TileConfig& cfg = em.tile_config;
    cfg = TileConfig{};
    cfg.tiles[kGrass] = {"", true};
    cfg.tiles[kRock] = {"", false};
    cfg.tile_visuals[kGrass] = {0, 0, 0.36f, 0.44f, 0.31f}; // muted grass green
    cfg.tile_visuals[kRock] = {0, 0, 0.34f, 0.35f, 0.38f};  // cool stone grey
}
} // namespace world_init
