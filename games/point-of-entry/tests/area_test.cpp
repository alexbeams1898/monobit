#include "AreaLoader.h"
#include "ecs/EntityManager.h"
#include "systems/TravelSystem.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// The importer's promises: a level loads exactly as painted, walkability is
// the tileset's Solid tag, entities become typed objects at world centres,
// and a door's trigger cannot be stepped over or bounced back through.

namespace
{

std::string tempPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

// A minimal project authored at the native 16px grid: one 2-column tileset
// (cell 0 floor, cell 1 Solid wall) and one 4x3 level. Cell (0,0) wall;
// (1,0) floor with a stacked wall (the stack must block); (1,1) floor. A
// Door, a PlayerStart, and an entity no builder knows.
void writeFixture(const std::string& path)
{
    std::ofstream out(path);
    out << R"({
        "defs": {
            "tilesets": [ {
                "uid": 1,
                "relPath": "../assets/tilesets/placeholder.png",
                "__cWid": 2,
                "enumTags": [ { "enumValueId": "Solid", "tileIds": [1] } ]
            } ]
        },
        "levels": [ {
            "identifier": "Fixture",
            "layerInstances": [
                {
                    "__identifier": "Entities",
                    "entityInstances": [
                        { "__identifier": "Door", "px": [32, 16],
                          "width": 16, "height": 16,
                          "fieldInstances": [
                              { "__identifier": "id", "__value": "fix_a" },
                              { "__identifier": "target", "__value": "fix_b" }
                          ] },
                        { "__identifier": "PlayerStart", "px": [16, 16],
                          "width": 8, "height": 8, "fieldInstances": [] },
                        { "__identifier": "MarkerTest", "px": [0, 32],
                          "width": 16, "height": 16, "fieldInstances": [] }
                    ]
                },
                {
                    "__identifier": "Ground",
                    "__gridSize": 16, "__cWid": 4, "__cHei": 3,
                    "gridTiles": [
                        { "px": [0, 0],  "src": [16, 0] },
                        { "px": [16, 0], "src": [0, 0] },
                        { "px": [16, 0], "src": [16, 0] },
                        { "px": [16, 16], "src": [0, 0] }
                    ]
                }
            ]
        } ]
    })";
}

} // namespace

TEST_CASE("a level loads exactly as painted", "[area]")
{
    const std::string path = tempPath("poe_world_fixture.ldtk");
    writeFixture(path);

    const area::Data d = area::loadLevel(path, "Fixture");
    REQUIRE(d.ok);
    CHECK(d.name == "Fixture");
    CHECK(d.width == 4);
    CHECK(d.height == 3);
    // 16px authoring doubles to the 32px world; the atlas stays at 16.
    CHECK(d.tile_size == 32);
    CHECK(d.config.atlas_tile_size == 16);

    // (0,0): the wall cell, Solid-tagged -> blocked.
    CHECK(d.tiles[0].tile_id == 1);
    CHECK_FALSE(d.tiles[0].walkable);
    // (1,0): floor base with a wall stacked on it -- the stack blocks, and
    // the stacked tile rides the decoration layer in paint order.
    CHECK(d.tiles[1].tile_id == 0);
    CHECK_FALSE(d.tiles[1].walkable);
    REQUIRE(d.decoration.size() == 1);
    CHECK(d.decoration[0].cell == 1);
    // (1,1): plain floor walks; an unpainted cell is void and blocked.
    CHECK(d.tiles[5].walkable);
    CHECK_FALSE(d.tiles[2].walkable);

    // The tileset lands in the config: atlas path resolved against the
    // project file's own location.
    const std::string suffix = "assets/tilesets/placeholder.png";
    REQUIRE(d.config.tileset_path.size() >= suffix.size());
    CHECK(d.config.tileset_path.compare(d.config.tileset_path.size() - suffix.size(), suffix.size(),
                                        suffix) == 0);
    CHECK(d.config.tile_visuals.count(0) == 1);
    CHECK(d.config.tile_visuals.count(1) == 1);
}

TEST_CASE("entities become typed objects at world centres", "[area]")
{
    const std::string path = tempPath("poe_world_fixture.ldtk");
    writeFixture(path);
    const area::Data d = area::loadLevel(path, "Fixture");
    REQUIRE(d.ok);
    REQUIRE(d.objects.size() == 3);

    CHECK(d.objects[0].type == "door");
    CHECK(d.objects[0].x == 80.0f); // authoring px [32,16] doubled + half a 32px body
    CHECK(d.objects[0].y == 48.0f);
    CHECK(d.objects[0].w == 32.0f);
    CHECK(d.objects[0].props.value("id", std::string{}) == "fix_a");
    CHECK(d.objects[0].props.value("target", std::string{}) == "fix_b");
    // PascalCase identifiers lower mechanically to builder keys.
    CHECK(d.objects[1].type == "player_start");
    CHECK(d.objects[2].type == "marker_test");
}

TEST_CASE("levels() lists the project's levels; a wrong name refuses", "[area]")
{
    const std::string path = tempPath("poe_world_fixture.ldtk");
    writeFixture(path);
    const std::vector<std::string> ls = area::levels(path);
    REQUIRE(ls.size() == 1);
    CHECK(ls[0] == "Fixture");
    CHECK_FALSE(area::loadLevel(path, "NoSuchLevel").ok);
}

TEST_CASE("build fills the map and reaches the registered builder", "[area]")
{
    const std::string path = tempPath("poe_world_fixture.ldtk");
    writeFixture(path);
    const area::Data d = area::loadLevel(path, "Fixture");
    REQUIRE(d.ok);

    static std::vector<std::pair<float, float>> sSeen;
    sSeen.clear();
    area::registerBuilder("marker_test", [](EntityManager&, const area::Object& o)
                          { sSeen.emplace_back(o.x, o.y); });

    EntityManager em;
    REQUIRE(area::build(em, d)); // door/player_start/unknown types log, never derail
    CHECK(em.tile_map.width == 4);
    CHECK(em.tile_map.tile_size == 32);
    CHECK_FALSE(em.tile_map.at(0, 0).walkable);
    CHECK(em.tile_map.at(1, 1).walkable);
    CHECK_FALSE(em.tile_config.tileset_path.empty());
    REQUIRE(sSeen.size() == 1);
    CHECK(sSeen[0].first == 16.0f);
    CHECK(sSeen[0].second == 80.0f);
}

TEST_CASE("the door trigger is swept, not sampled", "[travel]")
{
    // The rect is one 32px tile at (40,40); the body is 8x6 half-extents.
    const float rx = 40.0f;
    const float ry = 40.0f;
    const float rw = 32.0f;
    const float rh = 32.0f;

    // Walking across it fires.
    CHECK(travel::sweptHit(0.0f, 56.0f, 100.0f, 56.0f, 8.0f, 6.0f, rx, ry, rw, rh));
    // A tick large enough to step clean over the strip still fires.
    CHECK(travel::sweptHit(0.0f, 56.0f, 400.0f, 56.0f, 8.0f, 6.0f, rx, ry, rw, rh));
    // Passing well clear does not.
    CHECK_FALSE(travel::sweptHit(0.0f, 200.0f, 100.0f, 200.0f, 8.0f, 6.0f, rx, ry, rw, rh));
    // Standing still inside it: hit (the latch, not the sweep, prevents re-fire).
    CHECK(travel::sweptHit(56.0f, 56.0f, 56.0f, 56.0f, 8.0f, 6.0f, rx, ry, rw, rh));
    // Standing still outside it: nothing.
    CHECK_FALSE(travel::sweptHit(0.0f, 0.0f, 0.0f, 0.0f, 8.0f, 6.0f, rx, ry, rw, rh));
    // Grazing within the body's half-extent counts -- the body is a box, not a point.
    CHECK(travel::sweptHit(0.0f, 35.0f, 100.0f, 35.0f, 8.0f, 6.0f, rx, ry, rw, rh));
}
