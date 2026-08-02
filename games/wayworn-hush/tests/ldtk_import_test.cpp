#include "LdtkImport.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

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
                          {"__identifier": "target_id", "__value": "room_door"},
                          {"__identifier": "facing", "__value": "south"}]},
      {"__identifier": "Npc", "iid": "n1", "px": [24, 40], "width": 16, "height": 16,
       "fieldInstances": [{"__identifier": "npc", "__value": "mom"},
                          {"__identifier": "facing", "__value": "west"},
                          {"__identifier": "encounter", "__value": "mom_talk"}]},
      {"__identifier": "Rug", "iid": "r1", "px": [32, 32], "width": 16, "height": 16,
       "__tile": {"x": 0, "y": 0, "w": 16, "h": 16},
       "fieldInstances": [{"__identifier": "plane", "__value": "floor"}]},
      {"__identifier": "Bed", "iid": "b1", "px": [48, 48], "width": 16, "height": 32,
       "__tile": {"x": 0, "y": 16, "w": 16, "h": 32},
       "fieldInstances": [{"__identifier": "plane", "__value": "cover"},
                          {"__identifier": "cover_height", "__value": 24}]},
      {"__identifier": "Blocker", "iid": "bl1", "px": [64, 64], "width": 16, "height": 8},
      {"__identifier": "Tv", "iid": "tv1", "px": [80, 32], "width": 16, "height": 16,
       "__tile": {"tilesetUid": 2, "x": 32, "y": 0, "w": 16, "h": 16},
       "fieldInstances": [{"__identifier": "sort_offset", "__value": 6}]}
    ]},
    {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 4, "__cHei": 4,
     "gridTiles": [{"px": [0, 0], "src": [16, 0]},
                   {"px": [0, 0], "src": [32, 0]},
                   {"px": [0, 0], "src": [48, 0]},
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
       "fieldInstances": [{"__identifier": "id", "__value": "room_door"},
                          {"__identifier": "target_id", "__value": "shore_door"},
                          {"__identifier": "facing", "__value": "north"}]}
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

TEST_CASE("The start level is wherever the id-less PlayerSpawn lives", "[ldtk]")
{
    // Shore holds the fixture's default spawn (Room's spawn is named) -- the spawn IS
    // the start; there is no config twin to drift.
    REQUIRE(ldtk::findStartLevel(writeFixture()) == "Shore");
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

TEST_CASE("A Warp parses as a centered box naming the door it arrives at", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.warps.size() == 1);
    const auto& w = shore.warps[0];
    // A door names a DOOR, not a level -- which level that is comes from
    // ldtk::warpIndex, so the map never states it twice.
    REQUIRE(w.id == "shore_door");
    REQUIRE(w.target_id == "room_door");
    REQUIRE(w.facing == "south");
    // px is the PIVOT point, here bottom-center (0.5,1): the box the author SEES
    // sits above-and-centered on px, and the import must agree with the editor.
    REQUIRE(w.w == Approx(32.0f)); // 16px authoring box -> 32 world
    REQUIRE(w.x == Approx(32.0f)); // pivot x IS the center x
    REQUIRE(w.y == Approx(16.0f)); // bottom edge at pivot y -> center half a box up
}

TEST_CASE("An Npc parses as a placement AND registers its talk encounter", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.npcs.size() == 1);
    REQUIRE(shore.npcs[0].npc == "mom");
    REQUIRE(shore.npcs[0].facing == "west");
    REQUIRE(shore.npcs[0].wx == Approx(48.0f)); // pivot point x2 -> where she stands
    REQUIRE(shore.npcs[0].wy == Approx(80.0f));

    // The `encounter` field makes the SAME placement an encounter box: talking is
    // observing, anchored to the character.
    bool found = false;
    for (const auto& enc : shore.encounters)
        if (enc.id == "mom_talk")
        {
            found = true;
            REQUIRE(enc.placement_id == "n1");
        }
    REQUIRE(found);

    // Npc entities never leak into the untyped-object pile.
    for (const auto& o : shore.objects)
        REQUIRE(o.type != "Npc");
}

TEST_CASE("A prop's plane is authored; cover_top splits a bed into on and under", "[ldtk]")
{
    // The fixture's Rug (plane=floor) and Bed (plane=cover, cover_top=8): one
    // authored bed becomes TWO sprites -- the part you lie ON (upper, floor
    // plane) and the blanket that ENCLOSES (lower, cover plane), split 16 world
    // px (8 source px) from the top. A floor prop never gets a collider.
    const ldtk::Region shore = loadFixture({});
    REQUIRE(shore.props.size() == 5); // rug, pillow+blanket (split bed), blocker, tv
    const auto& rug = shore.props[0];
    REQUIRE(rug.plane == ldtk::Prop::Plane::Floor);
    REQUIRE_FALSE(rug.col_solid);

    const auto& pillow = shore.props[1];
    const auto& blanket = shore.props[2];
    REQUIRE(pillow.plane == ldtk::Prop::Plane::Floor);
    REQUIRE(pillow.sh == 16);
    REQUIRE(blanket.plane == ldtk::Prop::Plane::Cover);
    REQUIRE(blanket.sh == 48);
    REQUIRE(blanket.sy == pillow.sy + 16); // the art continues where the split cut
    REQUIRE_FALSE(blanket.col_solid);      // a collider would ride the upper piece
}

TEST_CASE("A Blocker is a hand-placed solid box and nothing else", "[ldtk]")
{
    const ldtk::Region shore = loadFixture({});
    const auto& b = shore.props[3];
    REQUIRE(b.sw == 0); // no art -- pure collision
    REQUIRE(b.col_solid);
    REQUIRE(b.col_w == Approx(32.0f)); // 16x8 authoring box -> 32x16 world
    REQUIRE(b.col_h == Approx(16.0f));
}

TEST_CASE("A Warp with no target_id is dropped, not kept broken", "[ldtk]")
{
    // A door that names no door leads nowhere; keeping it would be a threshold the
    // player can cross into nothing.
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_ldtk_nowarp.ldtk").string();
    std::ofstream(path) << R"JSON({
 "defs": {"tilesets": [{"identifier": "Overworld", "uid": 1, "relPath": "Overworld.png",
                        "__cWid": 40, "tileGridSize": 16, "enumTags": []}]},
 "levels": [{"identifier": "Nowhere", "layerInstances": [
   {"__identifier": "Entities", "__type": "Entities", "entityInstances": [
     {"__identifier": "Warp", "iid": "w9", "px": [0, 0], "width": 16, "height": 16,
      "fieldInstances": [{"__identifier": "id", "__value": "dangling"}]}]},
   {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 2, "__cHei": 2,
    "gridTiles": [{"px": [0, 0], "src": [16, 0]}]}]}]})JSON";
    const surfaces::Config sc;
    const structures::Config st;
    const ldtk::Region r = ldtk::load(path, "missing_atlas.png", sc, st, "Nowhere");
    REQUIRE(r.warps.empty());
}

TEST_CASE("warpIndex maps every warp id in the project to its level", "[ldtk]")
{
    // What turns "this door arrives at that door" into a level to load -- so the
    // map states the destination ONCE, as a door name.
    const auto index = ldtk::warpIndex(writeFixture());
    REQUIRE(index.at("shore_door") == "Shore");
    REQUIRE(index.at("room_door") == "Room");
    REQUIRE(index.count("no_such_door") == 0);
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

TEST_CASE("A stacked cell splits into base + ordered decoration stamps", "[ldtk]")
{
    // Three gridTiles at the same px: the FIRST is the base, EVERY later one becomes
    // a decoration stamp in paint order -- the renderer composites the whole stack
    // exactly as the editor shows it, not just the topmost tile.
    const ldtk::Region r = loadFixture({});
    REQUIRE(r.map.decoration.size() == 2);
    const std::size_t cell = r.map.cellIndex(0, 0);
    REQUIRE(r.map.decoration[0].cell == cell);
    REQUIRE(r.map.decoration[1].cell == cell);
    REQUIRE(r.map.decoration[0].tile.tile_id == 2); // src (32,0) painted first...
    REQUIRE(r.map.decoration[1].tile.tile_id == 3); // ...src (48,0) drawn over it
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

TEST_CASE("A prop resolves the atlas of ITS OWN tileset, not the level's", "[ldtk]")
{
    // Furniture placed in a room painted with another sheet must draw (and
    // alpha-scan) from the sheet its tile actually comes from. The fixture's Tv
    // carries a tile from the Inner tileset (uid 2) inside Overworld-painted
    // Shore; the naming convention resolves Inner.png -> assets/tilesets/inner.png.
    const ldtk::Region shore = loadFixture("Shore");
    bool sawTv = false;
    for (const auto& p : shore.props)
    {
        if (p.sx == 64) // the Tv's tile (source x 32 -> render x 64)
        {
            REQUIRE(p.texture_path == "assets/tilesets/inner.png");
            // A prop RESTING on another sorts by where it SITS: sort_offset
            // (6 source px, x2) pushes the depth base past its supporter's.
            REQUIRE(p.sort_wy == Approx(32.0f * 2 + 6.0f * 2));
            sawTv = true;
        }
        else if (p.sw > 0)
        {
            // Same-sheet props carry the level's own resolved atlas.
            REQUIRE(p.texture_path == shore.config.tileset_path);
        }
    }
    REQUIRE(sawTv);
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

TEST_CASE("Every decoration stamp lands on a real cell", "[ldtk]")
{
    // The real map's decoration CONTENT churns as the world is painted, so only the
    // structural invariant is asserted here; stacking behaviour itself is pinned by
    // the fixture test above.
    const ldtk::Region r = loadRegion();
    for (const auto& d : r.map.decoration)
        REQUIRE(d.cell < r.map.tiles.size());
}

TEST_CASE("a cover prop's sort anchor is its own bottom edge, not over everyone", "[ldtk]")
{
    // The blanket covers a body LYING IN the bed, but must not drape over one
    // standing IN FRONT of it. Its sort anchor is its own bottom edge, so feet
    // below that line out-sort it and feet above it (in the bed) stay under it.
    // Spawning is what applies the anchor, so this reads the spawned Sprite.
    EntityManager em;
    const ldtk::Region shore = loadFixture("Shore");
    ldtk::spawnProps(em, shore);

    const ldtk::Prop* blanket = nullptr;
    for (const auto& p : shore.props)
        if (p.plane == ldtk::Prop::Plane::Cover)
            blanket = &p;
    REQUIRE(blanket != nullptr);
    const float bottomEdge = blanket->wy + static_cast<float>(blanket->sh) * 0.5f;

    bool found = false;
    for (auto [e, spr] : em.registry().view<Sprite>().each())
        if (spr.use_sort_anchor && spr.sort_anchor == Approx(bottomEdge))
            found = true;
    REQUIRE(found);
    // A real world-Y, never the old "beyond anything" constant.
    REQUIRE(bottomEdge < 10000.0f);
}

TEST_CASE("a non-walkable tile painted OVER walkable ground still blocks", "[ldtk]")
{
    // Trees painted on top of grass: the grass is the cell's base and the tree is
    // a stacked tile, so reading walkability from the base alone would let the
    // player stroll through a forest. A cell is walkable only if every tile in it
    // is -- the surface tag travels with the art wherever it is painted.
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_ldtk_stacked.ldtk").string();
    std::ofstream(path) << R"JSON({
 "defs": {"tilesets": [{"identifier": "Overworld", "uid": 1, "relPath": "Overworld.png",
   "__cWid": 40, "tileGridSize": 16,
   "enumTags": [{"enumValueId": "Trees", "tileIds": [41]}]}]},
 "levels": [{"identifier": "Wood", "layerInstances": [
   {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 2, "__cHei": 1,
    "__tilesetDefUid": 1,
    "gridTiles": [{"px": [0, 0], "src": [16, 0]},
                  {"px": [0, 0], "src": [16, 16]},
                  {"px": [16, 0], "src": [16, 0]}]}]}]})JSON";
    surfaces::Config sc;
    surfaces::load(sc, "config/surfaces.json"); // Trees is authored non-walkable there
    const structures::Config st;
    const ldtk::Region r = ldtk::load(path, "missing_atlas.png", sc, st, "Wood");
    REQUIRE(r.ok);
    // Cell (0,0): grass base + a Trees tile stacked on it -> blocked.
    REQUIRE_FALSE(r.map.at(0, 0).walkable);
    // Cell (1,0): plain grass -> still walkable.
    REQUIRE(r.map.at(1, 0).walkable);
}

TEST_CASE("a Pickup lands where the author sees it, whatever its pivot", "[ldtk]")
{
    // LDtk's px is the entity's PIVOT point, not its top-left. A bottom-center
    // pivot (the editor default for placed things here) put pickups half a box
    // right and a box down when the position was computed by hand instead of
    // through boxCenter -- the item drew on the wrong tile.
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_ldtk_pickup.ldtk").string();
    std::ofstream(path) << R"JSON({
 "defs": {"tilesets": [{"identifier": "Overworld", "uid": 1, "relPath": "Overworld.png",
   "__cWid": 40, "tileGridSize": 16, "enumTags": []}]},
 "levels": [{"identifier": "Room", "layerInstances": [
   {"__identifier": "Pickups", "__type": "Entities", "entityInstances": [
     {"__identifier": "Pickup", "iid": "p1", "px": [32, 44], "width": 16, "height": 16,
      "__pivot": [0.5, 1],
      "fieldInstances": [{"__identifier": "item", "__value": "watch"},
                         {"__identifier": "sort_offset", "__value": 9}]}]},
   {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 4, "__cHei": 4,
    "gridTiles": [{"px": [0, 0], "src": [16, 0]}]}]}]})JSON";
    const surfaces::Config sc;
    const structures::Config st;
    const ldtk::Region r = ldtk::load(path, "missing_atlas.png", sc, st, "Room");
    REQUIRE(r.pickups.size() == 1);
    // px*2 = (64,88); a bottom-center pivot means that IS the box's bottom-center,
    // so the center sits half a box (16 world px) above it, x unchanged.
    REQUIRE(r.pickups[0].cx == Approx(64.0f));
    REQUIRE(r.pickups[0].cy == Approx(72.0f));
    // An item ON furniture sorts past what holds it (source px -> world x2), the
    // same authored offset a prop uses -- Y-sort has no height axis.
    REQUIRE(r.pickups[0].sort_offset == Approx(18.0f));
}

TEST_CASE("what a pickup yields is a field, not a type", "[ldtk]")
{
    // ONE placed thing: `item` gives exactly that, `yields` draws from a table, and the entity's
    // NAME is never read -- so an author can call it whatever they like and only the fields
    // decide. A placement may also carry its OWN art (the tileset region its entity shows in
    // the editor), which is how a pile of storm wood looks like itself rather than like a
    // stand-in for whatever it happens to give up.
    const auto path = (std::filesystem::temp_directory_path() / "wayworn_ldtk_yield.ldtk").string();
    std::ofstream(path) << R"JSON({
 "defs": {"tilesets": [{"identifier": "Overworld", "uid": 1, "relPath": "Overworld.png",
   "__cWid": 40, "tileGridSize": 16, "enumTags": []}]},
 "levels": [{"identifier": "Room", "layerInstances": [
   {"__identifier": "Pickups", "__type": "Entities", "entityInstances": [
     {"__identifier": "Pickup", "iid": "a", "px": [32, 16], "width": 16, "height": 16,
      "__pivot": [0.5, 1],
      "fieldInstances": [{"__identifier": "item", "__value": "watch"}]},
     {"__identifier": "AnythingAtAll", "iid": "b", "px": [64, 16], "width": 32, "height": 16,
      "__pivot": [0.5, 1],
      "__tile": {"tilesetUid": 1, "x": 592, "y": 16, "w": 32, "h": 16},
      "fieldInstances": [{"__identifier": "yields", "__value": "storm_debris"},
                         {"__identifier": "group", "__value": "richards_trail"},
                         {"__identifier": "clears_flag", "__value": "path_open"}]}]},
   {"__identifier": "Ground", "__type": "Tiles", "__gridSize": 16, "__cWid": 4, "__cHei": 4,
    "gridTiles": [{"px": [0, 0], "src": [16, 0]}]}]}]})JSON";
    const surfaces::Config sc;
    const structures::Config st;
    const ldtk::Region r = ldtk::load(path, "missing_atlas.png", sc, st, "Room");
    REQUIRE(r.pickups.size() == 2);

    // `item` -> yields exactly that, and names no art of its own (its icon speaks for it).
    REQUIRE(r.pickups[0].kind == ldtk::PickupPlacement::Kind::Item);
    REQUIRE(r.pickups[0].target == "watch");
    REQUIRE(r.pickups[0].sw == 0);

    // `yields` -> draws from that table, whatever the entity is CALLED.
    REQUIRE(r.pickups[1].kind == ldtk::PickupPlacement::Kind::Table);
    REQUIRE(r.pickups[1].target == "storm_debris");
    // Its own art: source px x2 into the render atlas, like a prop's.
    REQUIRE(r.pickups[1].sx == 1184);
    REQUIRE(r.pickups[1].sw == 64);
    REQUIRE(r.pickups[1].sh == 32);
    // And the clearing it belongs to.
    REQUIRE(r.pickups[1].group == "richards_trail");
    REQUIRE(r.pickups[1].clears_flag == "path_open");
}

TEST_CASE("clearingFlags finds every flag the map can raise", "[ldtk]")
{
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_ldtk_clearing.ldtk").string();
    std::ofstream(path) << R"JSON({
 "defs": {"tilesets": []},
 "levels": [
  {"identifier": "Yard", "layerInstances": [
   {"__identifier": "Pickups", "__type": "Entities", "entityInstances": [
     {"__identifier": "Pickup", "iid": "a", "px": [0, 0], "width": 16, "height": 16,
      "fieldInstances": [{"__identifier": "yields", "__value": "t"},
                         {"__identifier": "clears_flag", "__value": "path_open"}]}]}]},
  {"identifier": "Cave", "layerInstances": [
   {"__identifier": "Entities", "__type": "Entities", "entityInstances": [
     {"__identifier": "Pickup", "iid": "b", "px": [0, 0], "width": 16, "height": 16,
      "fieldInstances": [{"__identifier": "item", "__value": "seal"},
                         {"__identifier": "clears_flag", "__value": "gate_open"}]}]}]}]})JSON";
    const auto flags = ldtk::clearingFlags(path);
    REQUIRE(flags.count("path_open") == 1); // the Pickups layer
    REQUIRE(flags.count("gate_open") == 1); // and the Entities layer
    REQUIRE(flags.size() == 2);
}
