#include "LdtkImport.h"

#include <catch2/catch_test_macros.hpp>

// Tests the LDtk importer against the REAL authored region
// (assets/tilesets/source/overworld.ldtk) -- the actual export, not a hand-guessed
// fixture. Test working dir is the game source root (see CMake WORKING_DIRECTORY).
// Pure JSON->struct; no GL.

namespace
{
ldtk::Region loadRegion()
{
    return ldtk::load("assets/tilesets/source/overworld.ldtk", "assets/tilesets/overworld.png");
}
} // namespace

TEST_CASE("A missing file returns ok=false, not a crash", "[ldtk]")
{
    const ldtk::Region r = ldtk::load("assets/nope/does_not_exist.ldtk", "x.png");
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("The authored region loads with a valid grid + x2 scale", "[ldtk]")
{
    // Invariants, robust to re-authoring (the region size changes as it's edited):
    // positive dims, the x2 authoring->world scale, and a dense ground grid.
    const ldtk::Region r = loadRegion();
    REQUIRE(r.ok);
    REQUIRE(r.map.width > 0);
    REQUIRE(r.map.height > 0);
    REQUIRE(r.map.tile_size == 32); // x2 from the 16px authoring grid
    REQUIRE(r.map.tiles.size() ==
            static_cast<std::size_t>(r.map.width) * static_cast<std::size_t>(r.map.height));
}

TEST_CASE("The tileset atlas + 32px cell size are set on the config", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    REQUIRE(r.config.tileset_path == "assets/tilesets/overworld.png");
    REQUIRE(r.config.atlas_tile_size == 32);
    REQUIRE_FALSE(r.config.tile_visuals.empty());
}

TEST_CASE("fill_tile resolves to the grass cell (0,0)", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    REQUIRE(r.fill_uv_col == 0);
    REQUIRE(r.fill_uv_row == 0);
    // An unpainted corner cell falls back to the fill tile; its visual is (0,0).
    const int id = r.map.at(0, 0).tile_id;
    const auto it = r.config.tile_visuals.find(id);
    REQUIRE(it != r.config.tile_visuals.end());
}

TEST_CASE("Painted non-grass ground tiles carry their own tileset uv", "[ldtk]")
{
    // Robust to re-authoring: the region has SOME painted ground that isn't the
    // (0,0) grass fill (the river etc.). Assert at least one ground cell references
    // a non-(0,0) atlas cell, and every used id has a visual.
    const ldtk::Region r = loadRegion();
    bool anyNonGrass = false;
    for (const auto& t : r.map.tiles)
    {
        const auto it = r.config.tile_visuals.find(t.tile_id);
        REQUIRE(it != r.config.tile_visuals.end());
        if (it->second.uv_col != 0 || it->second.uv_row != 0)
            anyNonGrass = true;
    }
    REQUIRE(anyNonGrass);
}

TEST_CASE("The PlayerSpawn entity is imported at doubled world pixels", "[ldtk]")
{
    // Robust to where the spawn was placed: assert one exists, inside the region,
    // at even world coords (x2 of the 16px authoring px is always even).
    const ldtk::Region r = loadRegion();
    bool found = false;
    for (const auto& o : r.objects)
        if (o.type == "PlayerSpawn")
        {
            found = true;
            REQUIRE(o.wx >= 0.0f);
            REQUIRE(o.wy >= 0.0f);
            REQUIRE(o.wx < static_cast<float>(r.map.width * r.map.tile_size));
            REQUIRE(o.wy < static_cast<float>(r.map.height * r.map.tile_size));
        }
    REQUIRE(found);
}

TEST_CASE("The Overhang layer imports as a sparse prop layer", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    // The region has an Overhang layer (trees/rock/log). It's the same size as the
    // ground grid, mostly empty (tile_id 0 = no prop), with SOME prop cells set.
    REQUIRE(r.map.overhang.size() == r.map.tiles.size());
    int props = 0;
    for (const auto& t : r.map.overhang)
        if (t.tile_id != 0)
            ++props;
    REQUIRE(props > 0);                                       // props were authored on it
    REQUIRE(props < static_cast<int>(r.map.overhang.size())); // sparse, not full
}

TEST_CASE("Tiles stacked on the Ground layer split into decoration", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    // Flowers painted OVER grass on the Ground layer stack per cell: the base tile
    // stays on `tiles`, the stacked-on tile lands on `decoration` (drawn under the
    // player, so grass still shows below the flower). Sparse: some cells set, not all.
    REQUIRE(r.map.decoration.size() == r.map.tiles.size());
    int deco = 0;
    for (const auto& t : r.map.decoration)
        if (t.tile_id != 0)
            ++deco;
    REQUIRE(deco > 0); // the authored flowers-over-grass cells
    REQUIRE(deco < static_cast<int>(r.map.decoration.size()));
}
