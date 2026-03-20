#include "TileMap.h"
#include "TileMapLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// TileMapLoader / TileMap tests — no window, no GPU required.
//
// TileMapRenderer is not unit-tested here because it requires an OpenGL
// context. It is covered by running the game (integration test).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Room parsing
// ---------------------------------------------------------------------------

TEST_CASE("TileMapLoader parses room tile types correctly", "[tilemap]")
{
    // clang-format off
    const std::string text =
        "WWWWWWWWW\n"
        "W.......W\n"
        "W.......W\n"
        "W.......W\n"
        "WWWWWWWWW\n";
    // clang-format on

    const Room room = TileMapLoader::parseRoom(text, "room_small");

    REQUIRE(room.width == 9);
    REQUIRE(room.height == 5);

    // Corners must be Solid.
    REQUIRE(room.tiles[0 * 9 + 0] == TileMap::SOLID_ID); // top-left
    REQUIRE(room.tiles[0 * 9 + 8] == TileMap::SOLID_ID); // top-right
    REQUIRE(room.tiles[4 * 9 + 0] == TileMap::SOLID_ID); // bottom-left
    REQUIRE(room.tiles[4 * 9 + 8] == TileMap::SOLID_ID); // bottom-right

    // Interior must be Walkable.
    REQUIRE(room.tiles[1 * 9 + 1] == TileMap::WALKABLE_ID);
    REQUIRE(room.tiles[2 * 9 + 4] == TileMap::WALKABLE_ID);

    // Bottom-centre is solid (corridors punch through, no D marker needed).
    REQUIRE(room.tiles[4 * 9 + 4] == TileMap::SOLID_ID);
}

TEST_CASE("TileMapLoader parses Obstacle tiles", "[tilemap]")
{
    const std::string text = "WWW\n"
                             "WXW\n"
                             "WWW\n";

    const Room room = TileMapLoader::parseRoom(text, "obstacle_test");

    REQUIRE(room.width == 3);
    REQUIRE(room.height == 3);
    REQUIRE(room.tiles[1 * 3 + 1] == 3); // obstacle tile_id
}

// ---------------------------------------------------------------------------
// Walkability flags
// ---------------------------------------------------------------------------

TEST_CASE("TileMap walkability: walkable vs solid tiles", "[tilemap]")
{
    TileMap map;
    map.width = 3;
    map.height = 1;
    map.tiles = {
        {TileMap::WALKABLE_ID, true}, {TileMap::SOLID_ID, false}, {3, false}, // obstacle
    };

    REQUIRE(map.at(0, 0).walkable == true);
    REQUIRE(map.at(1, 0).walkable == false);
    REQUIRE(map.at(2, 0).walkable == false);
}

// ---------------------------------------------------------------------------
// Spawn points
// ---------------------------------------------------------------------------

TEST_CASE("TileMapLoader collects spawn points from room template", "[tilemap]")
{
    const std::string text = "WWWWW\n"
                             "W.E.W\n"
                             "W.C.W\n"
                             "WWWWW\n";

    const Room room = TileMapLoader::parseRoom(text, "spawn_test");

    REQUIRE(room.spawn_points.size() == 2);

    // E is at col=2, row=1
    const auto& e_sp = room.spawn_points[0];
    REQUIRE(e_sp.col == 2);
    REQUIRE(e_sp.row == 1);
    REQUIRE(e_sp.type == 'E');

    // C is at col=2, row=2
    const auto& c_sp = room.spawn_points[1];
    REQUIRE(c_sp.col == 2);
    REQUIRE(c_sp.row == 2);
    REQUIRE(c_sp.type == 'C');

    // Both E and C tiles should be Walkable in the grid (not a separate type).
    REQUIRE(room.tiles[1 * 5 + 2] == TileMap::WALKABLE_ID);
    REQUIRE(room.tiles[2 * 5 + 2] == TileMap::WALKABLE_ID);
}

// ---------------------------------------------------------------------------
// Viewport culling bounds
// ---------------------------------------------------------------------------

TEST_CASE("TileMapRenderer culling: correct tile range for camera at map centre", "[tilemap]")
{
    // Map: 80x60 tiles (2560x1920 px). Camera at (640, 480). Window: 1280x720.
    // Expected visible columns: floor((640 - 640) / 32) = 0 to floor((640+640)/32) = 40
    // Expected visible rows:    floor((480 - 360) / 32) = 3 to floor((480+360)/32) = 26

    constexpr float cam_x = 640.0f;
    constexpr float cam_y = 480.0f;
    constexpr int win_w = 1280;
    constexpr int win_h = 720;
    constexpr float ts = static_cast<float>(TileMap::TILE_SIZE);
    constexpr int map_w = 80;
    constexpr int map_h = 60;
    const float half_w = static_cast<float>(win_w) * 0.5f;
    const float half_h = static_cast<float>(win_h) * 0.5f;

    const int col_min = std::max(0, static_cast<int>(std::floor((cam_x - half_w) / ts)));
    const int col_max = std::min(map_w - 1, static_cast<int>(std::floor((cam_x + half_w) / ts)));
    const int row_min = std::max(0, static_cast<int>(std::floor((cam_y - half_h) / ts)));
    const int row_max = std::min(map_h - 1, static_cast<int>(std::floor((cam_y + half_h) / ts)));

    REQUIRE(col_min == 0);
    REQUIRE(col_max == 40);
    REQUIRE(row_min == 3);
    REQUIRE(row_max == 26);
}

TEST_CASE("TileMapRenderer culling: range clamps to map bounds at edges", "[tilemap]")
{
    // Camera near the top-left corner. Some of the visible window is outside the map.
    constexpr float cam_x = 0.0f;
    constexpr float cam_y = 0.0f;
    constexpr int win_w = 1280;
    constexpr int win_h = 720;
    constexpr float ts = static_cast<float>(TileMap::TILE_SIZE);
    constexpr int map_w = 80;
    constexpr int map_h = 60;
    const float half_w = static_cast<float>(win_w) * 0.5f;
    const float half_h = static_cast<float>(win_h) * 0.5f;

    const int col_min = std::max(0, static_cast<int>(std::floor((cam_x - half_w) / ts)));
    const int col_max = std::min(map_w - 1, static_cast<int>(std::floor((cam_x + half_w) / ts)));
    const int row_min = std::max(0, static_cast<int>(std::floor((cam_y - half_h) / ts)));
    const int row_max = std::min(map_h - 1, static_cast<int>(std::floor((cam_y + half_h) / ts)));

    REQUIRE(col_min == 0); // clamped — not negative
    REQUIRE(col_max == 20);
    REQUIRE(row_min == 0); // clamped — not negative
    REQUIRE(row_max == 11);
}

// ---------------------------------------------------------------------------
// Tile-based collision (no ECS colliders for solid tiles)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// PlacedRoom + findRoomAt
// ---------------------------------------------------------------------------

TEST_CASE("findRoomAt returns correct room index inside a room", "[tilemap]")
{
    TileMap map;
    map.width = 20;
    map.height = 20;
    map.tiles.assign(400, {TileMap::SOLID_ID, false});

    // Place a room at tile (5, 3) with size 8x6.
    map.placed_rooms.push_back({5, 3, 8, 6});

    // World center of tile (7, 5) = (7*32+16, 5*32+16) = (240, 176).
    REQUIRE(map.findRoomAt(240.0f, 176.0f) == 0);
}

TEST_CASE("findRoomAt returns -1 in a corridor", "[tilemap]")
{
    TileMap map;
    map.width = 20;
    map.height = 20;
    map.tiles.assign(400, {TileMap::SOLID_ID, false});

    map.placed_rooms.push_back({5, 3, 8, 6});

    // Tile (1, 1) = (48, 48) — outside the room.
    REQUIRE(map.findRoomAt(48.0f, 48.0f) == -1);
}

TEST_CASE("findRoomAt distinguishes multiple rooms", "[tilemap]")
{
    TileMap map;
    map.width = 40;
    map.height = 20;
    map.tiles.assign(800, {TileMap::SOLID_ID, false});

    map.placed_rooms.push_back({2, 2, 6, 4});   // room 0
    map.placed_rooms.push_back({20, 5, 10, 8}); // room 1

    // Inside room 0: tile (4, 3) = (144, 112).
    REQUIRE(map.findRoomAt(144.0f, 112.0f) == 0);

    // Inside room 1: tile (25, 9) = (816, 304).
    REQUIRE(map.findRoomAt(816.0f, 304.0f) == 1);

    // Between rooms: tile (12, 4) = (400, 144).
    REQUIRE(map.findRoomAt(400.0f, 144.0f) == -1);
}

TEST_CASE("generate populates placed_rooms", "[tilemap]")
{
    EntityManager em;

    TileMapLoader::generate(em, "config/tilemap.json", "config/rooms", 12345u);

    // With room_count=6, we should have at least some rooms placed.
    REQUIRE(em.tile_map.placed_rooms.size() >= 2);

    // Each placed room should be within map bounds.
    for (const auto& rm : em.tile_map.placed_rooms)
    {
        REQUIRE(rm.col >= 0);
        REQUIRE(rm.row >= 0);
        REQUIRE(rm.col + rm.width <= em.tile_map.width);
        REQUIRE(rm.row + rm.height <= em.tile_map.height);
    }
}

// ---------------------------------------------------------------------------
// Full generation test
// ---------------------------------------------------------------------------

TEST_CASE("TileMapLoader::generate builds TileMap with valid layout", "[tilemap]")
{
    // Uses real config + room files. CTest WORKING_DIRECTORY is the project root,
    // so the paths resolve correctly.
    EntityManager em;

    auto [px, py] = TileMapLoader::generate(em, "config/tilemap.json", "config/rooms",
                                            /*seed=*/12345u);

    REQUIRE(em.tile_map.valid());
    REQUIRE(em.tile_map.seed == 12345u);

    // Tile (0,0) is always Solid — the map starts fully solid and rooms are
    // carved out; corners are never inside a placed room.
    REQUIRE(em.tile_map.at(0, 0).tile_id == TileMap::SOLID_ID);
    REQUIRE(em.tile_map.at(0, 0).walkable == false);

    // Solid tiles are the physics source via the tile map — no ECS Collider
    // entities are spawned. This keeps the ECS sparse set small and lets
    // MovementSystem / CollisionSystem / FlowFieldSystem do O(~4) tile lookups
    // instead of O(n_wall) entity scans.
    REQUIRE(em.registry().view<Collider>().size() == 0u);

    // Player spawn must be inside the map world bounds.
    const float map_world_w = static_cast<float>(em.tile_map.width * TileMap::TILE_SIZE);
    const float map_world_h = static_cast<float>(em.tile_map.height * TileMap::TILE_SIZE);
    REQUIRE(px >= 0.0f);
    REQUIRE(px <= map_world_w);
    REQUIRE(py >= 0.0f);
    REQUIRE(py <= map_world_h);
}
