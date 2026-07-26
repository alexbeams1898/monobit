#include "LdtkImport.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

// Tests the LDtk importer against the REAL authored region
// (assets/tilesets/source/overworld.ldtk) -- the actual export, not a hand-guessed
// fixture. Test working dir is the game source root (see CMake WORKING_DIRECTORY).
// Pure JSON->struct; no GL.

namespace
{
ldtk::Region loadRegion()
{
    surfaces::Config sc;
    surfaces::load(sc, "config/surfaces.json");
    structures::Config st;
    structures::load(st, "config/structures.json");
    return ldtk::load("assets/tilesets/source/overworld.ldtk", "assets/tilesets/overworld.png", sc,
                      st);
}
} // namespace

TEST_CASE("A missing file returns ok=false, not a crash", "[ldtk]")
{
    const surfaces::Config sc;
    const structures::Config st;
    const ldtk::Region r = ldtk::load("assets/nope/does_not_exist.ldtk", "x.png", sc, st);
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

TEST_CASE("Every ground tile id resolves to a visual", "[ldtk]")
{
    // The invariant that survives re-authoring: whatever is painted, every used id
    // has a uv. (WHICH tiles are painted churns as the world is authored -- specific
    // uv behaviour is pinned by the fixture tests below, not the live map.)
    const ldtk::Region r = loadRegion();
    for (const auto& t : r.map.tiles)
        REQUIRE(r.config.tile_visuals.find(t.tile_id) != r.config.tile_visuals.end());
}

// --- Multi-level / warp / spawn fixture -------------------------------------------------
// A minimal two-level project written to a temp file. The real map churns as the world is
// painted (entities come and go), so the importer's FEATURE surface -- level selection,
// spawns, warps, level fields, decoration stacking -- is pinned against this fixture
// instead of whatever happens to be authored today. The atlas PNG deliberately doesn't
// exist: the importer skips atlas-dependent validation and keeps every tile.
namespace
{
std::string writeFixture()
{
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_ldtk_fixture.ldtk").string();
    static const char* kJson = R"JSON({
 "defs": {"tilesets": [{"identifier": "Overworld", "uid": 1, "relPath": "Overworld.png",
                        "__cWid": 40, "tileGridSize": 16, "enumTags": []},
                       {"identifier": "Inner", "uid": 2, "relPath": "Inner.png",
                        "__cWid": 40, "tileGridSize": 16, "enumTags": []}]},
 "levels": [
  {"identifier": "Shore",
   "fieldInstances": [{"__identifier": "music", "__value": "amb_shore"},
                      {"__identifier": "interior", "__value": false}],
   "layerInstances": [
    {"__identifier": "Entities", "__type": "Entities", "entityInstances": [
      {"__identifier": "PlayerSpawn", "iid": "s1", "px": [8, 8],
       "fieldInstances": [{"__identifier": "id", "__value": ""},
                          {"__identifier": "facing", "__value": ""}]},
      {"__identifier": "Warp", "iid": "w1", "px": [16, 16], "width": 16, "height": 16,
       "__pivot": [0.5, 1],
       "fieldInstances": [{"__identifier": "id", "__value": "shore_door"},
                          {"__identifier": "target_level", "__value": "Room"},
                          {"__identifier": "target_spawn", "__value": "from_door"},
                          {"__identifier": "facing", "__value": "south"}]}
    ]},
    {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 4, "__cHei": 4,
     "gridTiles": [{"px": [0, 0], "src": [16, 0]},
                   {"px": [0, 0], "src": [32, 0]},
                   {"px": [16, 0], "src": [16, 0]}]}
   ]},
  {"identifier": "Room",
   "fieldInstances": [{"__identifier": "interior", "__value": true}],
   "layerInstances": [
    {"__identifier": "Entities", "__type": "Entities", "entityInstances": [
      {"__identifier": "PlayerSpawn", "iid": "s2", "px": [4, 4],
       "fieldInstances": [{"__identifier": "id", "__value": "from_door"},
                          {"__identifier": "facing", "__value": "north"}]},
      {"__identifier": "Warp", "iid": "w2", "px": [0, 0], "width": 16, "height": 16,
       "fieldInstances": [{"__identifier": "target_level", "__value": ""}]}
    ]},
    {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 2, "__cHei": 2,
     "__tilesetDefUid": 2,
     "gridTiles": [{"px": [0, 0], "src": [16, 0]}]}
   ]}
 ]})JSON";
    std::ofstream f(path);
    f << kJson;
    return path;
}

ldtk::Region loadFixture(const std::string& level)
{
    const surfaces::Config sc;
    const structures::Config st;
    return ldtk::load(writeFixture(), "missing_atlas.png", sc, st, level);
}
} // namespace

TEST_CASE("An empty level name loads the project's first level", "[ldtk]")
{
    const ldtk::Region r = loadFixture({});
    REQUIRE(r.ok);
    REQUIRE(r.level_id == "Shore");
}

TEST_CASE("A level is selected by identifier; a missing one fails", "[ldtk]")
{
    const ldtk::Region room = loadFixture("Room");
    REQUIRE(room.ok);
    REQUIRE(room.level_id == "Room");
    REQUIRE(room.map.width == 2);

    REQUIRE_FALSE(loadFixture("Nowhere").ok);
}

TEST_CASE("PlayerSpawn parses into spawns (id + facing), never into objects", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.spawns.size() == 1);
    REQUIRE(shore.spawns[0].id.empty()); // the new-game default spawn
    REQUIRE(shore.spawns[0].wx == Approx(16.0f));
    REQUIRE(shore.spawns[0].wy == Approx(16.0f)); // x2 authoring -> world

    const ldtk::Region room = loadFixture("Room");
    REQUIRE(room.spawns.size() == 1);
    REQUIRE(room.spawns[0].id == "from_door");
    REQUIRE(room.spawns[0].facing == "north");

    for (const auto& o : shore.objects)
        REQUIRE(o.type != "PlayerSpawn");
}

TEST_CASE("A Warp parses as a centered box with its targets", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.warps.size() == 1);
    const auto& w = shore.warps[0];
    REQUIRE(w.target_level == "Room");
    REQUIRE(w.id == "shore_door");
    REQUIRE(w.target == "from_door"); // read from the legacy target_spawn field name
    REQUIRE(w.facing == "south");
    // px is the PIVOT point, here bottom-center (0.5,1): the box the author SEES
    // sits above-and-centered on px, and the import must agree with the editor.
    REQUIRE(w.w == Approx(32.0f)); // 16px authoring box -> 32 world
    REQUIRE(w.x == Approx(32.0f)); // pivot x IS the center x
    REQUIRE(w.y == Approx(16.0f)); // bottom edge at pivot y -> center half a box up
}

TEST_CASE("A Warp with no target_level is dropped, not kept broken", "[ldtk]")
{
    const ldtk::Region room = loadFixture("Room");
    REQUIRE(room.warps.empty());
}

TEST_CASE("A level painted with its own tileset resolves that tileset's atlas", "[ldtk]")
{
    // Room's ground names the Inner tileset (uid 2): the importer resolves the x2
    // render-atlas twin by convention (source Inner.png -> assets/tilesets/inner.png,
    // which exists in the repo). This is what lets an interior render from its own
    // sheet. Shore's ground names NO tileset -> the legacy Overworld match applies,
    // which resolves overworld's atlas the same way.
    const ldtk::Region room = loadFixture("Room");
    REQUIRE(room.config.tileset_path == "assets/tilesets/inner.png");

    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.config.tileset_path == "assets/tilesets/overworld.png");
}

TEST_CASE("Per-level fields parse: music and interior", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.music == "amb_shore");
    REQUIRE_FALSE(shore.interior);

    const ldtk::Region room = loadFixture("Room");
    REQUIRE(room.music.empty());
    REQUIRE(room.interior);
}

TEST_CASE("A painted tile keeps its atlas uv through import", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    bool sawCol1 = false;
    for (const auto& [id, vis] : shore.config.tile_visuals)
        if (vis.uv_col == 1 && vis.uv_row == 0)
            sawCol1 = true;
    REQUIRE(sawCol1); // src (16,0) in the 16px source -> atlas cell (1,0)
}

TEST_CASE("A stacked cell splits into base + decoration", "[ldtk]")
{
    // Two gridTiles at the same px: the FIRST is the base, the second lands on the
    // decoration layer (flowers over grass, drawn under the player).
    const ldtk::Region r = loadFixture({});
    REQUIRE(r.map.decoration.size() == r.map.tiles.size());
    int deco = 0;
    for (const auto& t : r.map.decoration)
        if (t.tile_id != 0)
            ++deco;
    REQUIRE(deco == 1);
}

TEST_CASE("Tile-carrying entities import as Y-sorted props", "[ldtk]")
{
    // Props come from LDtk ENTITIES that carry a tileset region (trees/rocks placed,
    // not painted) -- each a single Y-sorted sprite. May be empty until authored;
    // when present, each has a non-empty atlas src rect and a base sort key.
    const ldtk::Region r = loadRegion();
    for (const auto& p : r.props)
    {
        REQUIRE(p.sw > 0);
        REQUIRE(p.sh > 0);
        REQUIRE(p.sort_wy >= 0.0f); // base world-Y for depth sort
    }
}

TEST_CASE("Prop colliders are derived from the sprite footprint (trunk), not the box", "[ldtk]")
{
    // The collider must hug the drawn footprint: for a tree, the narrow trunk at the
    // base -- NOT the full sprite box (which would collide with the transparent margin)
    // and NOT the whole opaque bounds (which would collide with the walk-through
    // canopy). Robust to which props are authored: only asserts on solid ones.
    const ldtk::Region r = loadRegion();
    for (const auto& p : r.props)
    {
        if (!p.col_solid)
            continue;
        // Tighter than the sprite in BOTH axes (the footprint is a sub-box).
        REQUIRE(p.col_w > 0.0f);
        REQUIRE(p.col_h > 0.0f);
        REQUIRE(p.col_w < static_cast<float>(p.sw));
        REQUIRE(p.col_h < static_cast<float>(p.sh));
        // The footprint sits at the BASE, not up in the canopy: its bottom edge is near
        // the sprite's bottom. A few px of slack tolerates art with transparent padding
        // at the very base (e.g. a rock with a 1px skirt) -- what matters is it's anchored
        // low, not that it's pixel-exact.
        const float spriteBottom = p.wy + static_cast<float>(p.sh) * 0.5f;
        const float colBottom = p.col_cy + p.col_h * 0.5f;
        REQUIRE(colBottom == Catch::Approx(spriteBottom).margin(4.0f));
        // The footprint is fully inside the sprite box horizontally.
        const float spriteLeft = p.wx - static_cast<float>(p.sw) * 0.5f;
        const float spriteRight = p.wx + static_cast<float>(p.sw) * 0.5f;
        REQUIRE(p.col_cx - p.col_w * 0.5f >= spriteLeft - 0.5f);
        REQUIRE(p.col_cx + p.col_w * 0.5f <= spriteRight + 0.5f);
    }
}

TEST_CASE("The decoration layer always matches the grid", "[ldtk]")
{
    // The real map's decoration CONTENT churns as the world is painted (the current
    // repaint has none), so only the structural invariant is asserted here; stacking
    // behaviour itself is pinned by the fixture test above.
    const ldtk::Region r = loadRegion();
    REQUIRE(r.map.decoration.size() == r.map.tiles.size());
}
