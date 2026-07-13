#include "LdtkImport.h"

#include <catch2/catch_test_macros.hpp>

// Tests the LDtk importer against the REAL authored region
// (assets/regions/overworld.ldtk) -- the actual export, not a hand-guessed
// fixture. Test working dir is the game source root (see CMake WORKING_DIRECTORY).
// Pure JSON->struct; no GL.

namespace
{
ldtk::Region loadRegion()
{
    return ldtk::load("assets/regions/overworld.ldtk", "assets/tilesets/overworld.png");
}
} // namespace

TEST_CASE("A missing file returns ok=false, not a crash", "[ldtk]")
{
    const ldtk::Region r = ldtk::load("assets/regions/does_not_exist.ldtk", "x.png");
    REQUIRE_FALSE(r.ok);
}

TEST_CASE("The authored region loads with the expected grid + scale", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    REQUIRE(r.ok);
    REQUIRE(r.map.width == 16); // 256px / 16px authoring cells
    REQUIRE(r.map.height == 16);
    REQUIRE(r.map.tile_size == 32); // x2 from the 16px authoring grid
    REQUIRE(r.map.tiles.size() == 16 * 16);
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

TEST_CASE("A painted water cell maps to its tileset uv", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    // The river band was painted with water tiles from tileset rows 3-4 (src y=48
    // and y=64 -> uv_row 3/4). Cell (0,4) [px 0,64] carries water src (0,128)->uv(0,8)
    // on top (top-tile-wins); assert the cell's visual row is a water/edge row, not
    // grass row 0.
    const int id = r.map.at(0, 4).tile_id;
    const auto it = r.config.tile_visuals.find(id);
    REQUIRE(it != r.config.tile_visuals.end());
    REQUIRE(it->second.uv_row > 0); // not the (0,0) grass fill
}

TEST_CASE("The PlayerSpawn entity is imported at world pixels", "[ldtk]")
{
    const ldtk::Region r = loadRegion();
    bool found = false;
    for (const auto& o : r.objects)
        if (o.type == "PlayerSpawn")
        {
            found = true;
            // LDtk px [128,160] (16px grid) -> world [256,320] (x2 to the 32px grid).
            REQUIRE(o.wx == 256.0f);
            REQUIRE(o.wy == 320.0f);
        }
    REQUIRE(found);
}
