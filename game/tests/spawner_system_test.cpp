#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ops/SpawnUtils.h"
#include "systems/SpawnerSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// SpawnerSystem tests — no window, no GPU required.
//
// CTest runs with WORKING_DIRECTORY = project root so relative paths like
// "config/spawns/initial_spawn.json" resolve correctly.
// ---------------------------------------------------------------------------

TEST_CASE("SpawnerSystem spawns correct number of entities", "[spawner]")
{
    EntityManager em;
    const int n = SpawnerSystem::load(em, "config/spawns/initial_spawn.json");
    REQUIRE(n == 4);
}

TEST_CASE("SpawnerSystem spawns entities with correct tag", "[spawner]")
{
    EntityManager em;
    SpawnerSystem::load(em, "config/spawns/initial_spawn.json");

    int count = 0;
    em.registry().view<Tag>().each(
        [&](const Tag& tag)
        {
            if (tag.name == "skeleton")
                ++count;
        });
    REQUIRE(count == 4);
}

TEST_CASE("SpawnerSystem applies position overrides — no two enemies share a position", "[spawner]")
{
    EntityManager em;
    SpawnerSystem::load(em, "config/spawns/initial_spawn.json");

    std::vector<std::pair<float, float>> positions;
    auto view = em.registry().view<Transform, Tag>();
    for (auto entity : view)
    {
        const auto& tag = view.get<Tag>(entity);
        if (tag.name == "skeleton")
        {
            const auto& t = view.get<Transform>(entity);
            positions.push_back({t.x, t.y});
        }
    }

    REQUIRE(positions.size() == 4);
    for (size_t i = 0; i < positions.size(); ++i)
        for (size_t j = i + 1; j < positions.size(); ++j)
            REQUIRE(positions[i] != positions[j]);
}

TEST_CASE("SpawnerSystem returns 0 for missing file", "[spawner]")
{
    EntityManager em;
    const int n = SpawnerSystem::load(em, "config/spawns/nonexistent.json");
    REQUIRE(n == 0);
    REQUIRE(em.registry().view<Tag>().size() == 0);
}

// ---------------------------------------------------------------------------
// SpawnUtils room-scoped spawning tests
// ---------------------------------------------------------------------------

// Helper: build a small TileMap with one room for spawn tests.
static TileMap buildTestMap()
{
    // 20x20 tile map, one room at (5, 5) with size 10x10.
    TileMap tm;
    tm.width = 20;
    tm.height = 20;
    tm.tiles.assign(400, {TileMap::SOLID_ID, false});

    // Carve the room as walkable.
    for (int r = 5; r < 15; ++r)
        for (int c = 5; c < 15; ++c)
            tm.at(c, r) = {TileMap::WALKABLE_ID, true};

    tm.placed_rooms.push_back({5, 5, 10, 10});
    return tm;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("SpawnUtils: spawn position lands inside the player's room", "[spawn]")
{
    TileMap tm = buildTestMap();

    // Player at room center: tile (10, 10) = (336, 336).
    const float px = 336.0f;
    const float py = 336.0f;

    float sx = 0.0f;
    float sy = 0.0f;
    // Repeat a few times to check consistency.
    for (int i = 0; i < 20; ++i)
    {
        bool ok = SpawnUtils::findSpawnPosition(tm, px, py, 330.0f, 825.0f, sx, sy);
        REQUIRE(ok);

        // Spawn must be inside room bounds: tiles [5..14] -> world [160..480].
        const float ts = static_cast<float>(TileMap::TILE_SIZE);
        REQUIRE(sx >= 5.0f * ts);
        REQUIRE(sx <= 15.0f * ts);
        REQUIRE(sy >= 5.0f * ts);
        REQUIRE(sy <= 15.0f * ts);
    }
}

TEST_CASE("SpawnUtils: spawn position avoids minimum distance from player", "[spawn]")
{
    TileMap tm = buildTestMap();

    const float px = 336.0f;
    const float py = 336.0f;

    float sx = 0.0f;
    float sy = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        // nearDist=96 so room-scoped fallback applies (room is 320px wide).
        bool ok = SpawnUtils::findSpawnPosition(tm, px, py, 96.0f, 825.0f, sx, sy);
        REQUIRE(ok);

        const float dx = sx - px;
        const float dy = sy - py;
        const float dist = std::sqrt(dx * dx + dy * dy);
        REQUIRE(dist >= 96.0f); // nearDist passed to findSpawnPosition
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("SpawnUtils: corridor fallback picks nearest room", "[spawn]")
{
    // Two rooms, player in corridor between them.
    TileMap tm;
    tm.width = 40;
    tm.height = 10;
    tm.tiles.assign(400, {TileMap::SOLID_ID, false});

    // Room 0 at (2, 2) size 6x6.
    for (int r = 2; r < 8; ++r)
        for (int c = 2; c < 8; ++c)
            tm.at(c, r) = {TileMap::WALKABLE_ID, true};
    tm.placed_rooms.push_back({2, 2, 6, 6});

    // Room 1 at (30, 2) size 6x6.
    for (int r = 2; r < 8; ++r)
        for (int c = 30; c < 36; ++c)
            tm.at(c, r) = {TileMap::WALKABLE_ID, true};
    tm.placed_rooms.push_back({30, 2, 6, 6});

    // Corridor connecting them (row 4-5, cols 8-29).
    for (int c = 8; c < 30; ++c)
        for (int r = 4; r <= 5; ++r)
            tm.at(c, r) = {TileMap::WALKABLE_ID, true};

    // Player at corridor center, closer to room 0: tile (12, 4).
    const float px = 12.0f * 32.0f + 16.0f; // 400
    const float py = 4.0f * 32.0f + 16.0f;  // 144

    float sx = 0.0f;
    float sy = 0.0f;
    for (int i = 0; i < 20; ++i)
    {
        bool ok = SpawnUtils::findSpawnPosition(tm, px, py, 100.0f, 2000.0f, sx, sy);
        REQUIRE(ok);

        // Should spawn in room 0 (cols 2-7, rows 2-7) since it's closer.
        const int sc = static_cast<int>(sx) / 32;
        const int sr = static_cast<int>(sy) / 32;
        REQUIRE(sc >= 2);
        REQUIRE(sc < 8);
        REQUIRE(sr >= 2);
        REQUIRE(sr < 8);
    }
}
