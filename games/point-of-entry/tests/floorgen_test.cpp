#include "FloorGen.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <queue>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// The promises a generated floor must keep, swept across many seeds. Generation bugs are
// seed-shaped: one layout in fifty puts a spawn in rock or strands a room, and a playtest only
// ever sees the seed it happened to get. The sweep sees four hundred.
//
// CTest runs with WORKING_DIRECTORY at the project root, so the REAL config and room templates
// are what is being validated -- a synthetic room here would pass while the shipped one broke.

namespace
{

bool walkableAt(const TileMap& map, float x, float y)
{
    const int col = static_cast<int>(x) / map.tile_size;
    const int row = static_cast<int>(y) / map.tile_size;
    if (col < 0 || row < 0 || col >= map.width || row >= map.height)
        return false;
    return map
        .tiles[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) +
               static_cast<std::size_t>(col)]
        .walkable;
}

// Flood-fill from the spawn: every walkable tile must be reachable, or part of the floor --
// possibly the part holding a seep -- is sealed off and the swarm can never be cleared.
int reachableFrom(const TileMap& map, float x, float y)
{
    const int c0 = static_cast<int>(x) / map.tile_size;
    const int r0 = static_cast<int>(y) / map.tile_size;
    if (!walkableAt(map, x, y))
        return 0; // a solid start reaches nothing -- counting it would leak the flood through
    std::vector<char> seen(static_cast<std::size_t>(map.width * map.height), 0);
    std::queue<std::pair<int, int>> q;
    q.emplace(c0, r0);
    seen[static_cast<std::size_t>(r0 * map.width + c0)] = 1;
    int count = 0;
    while (!q.empty())
    {
        const auto [c, r] = q.front();
        q.pop();
        ++count;
        const int dc[4] = {1, -1, 0, 0};
        const int dr[4] = {0, 0, 1, -1};
        for (int i = 0; i < 4; ++i)
        {
            const int nc = c + dc[i];
            const int nr = r + dr[i];
            if (nc < 0 || nr < 0 || nc >= map.width || nr >= map.height)
                continue;
            const auto idx = static_cast<std::size_t>(nr * map.width + nc);
            if (seen[idx] || !map.tiles[idx].walkable)
                continue;
            seen[idx] = 1;
            q.emplace(nc, nr);
        }
    }
    return count;
}

int totalWalkable(const TileMap& map)
{
    int count = 0;
    for (const auto& t : map.tiles)
        if (t.walkable)
            ++count;
    return count;
}

} // namespace

TEST_CASE("every seed yields a floor that keeps its promises", "[floorgen]")
{
    for (unsigned seed = 1; seed <= 400; ++seed)
    {
        EntityManager em;
        const floorgen::Floor floor =
            floorgen::generate(em, "config/floor.json", "config/rooms", seed);
        INFO("seed " << seed);
        REQUIRE(floor.ok);

        // The player materialises here; solid rock would strand him before the game begins.
        CHECK(walkableAt(em.tile_map, floor.spawn_x, floor.spawn_y));

        // A seep in a wall spawns the swarm inside it, unkillable and unreachable.
        for (const auto& m : floor.markers)
        {
            INFO("marker '" << m.type << "' at " << m.x << "," << m.y);
            CHECK(walkableAt(em.tile_map, m.x, m.y));
        }

        // Nothing sealed off: a stranded room with a seep in it is a wave that cannot end.
        CHECK(reachableFrom(em.tile_map, floor.spawn_x, floor.spawn_y) ==
              totalWalkable(em.tile_map));
    }
}

// Hidden by the '.' tag: a diagnostic, not a promise. Prints a seed's floor as ASCII so a
// failing layout can be looked at instead of imagined.
TEST_CASE("dump one seed", "[.dump]")
{
    EntityManager em;
    const floorgen::Floor floor = floorgen::generate(em, "config/floor.json", "config/rooms", 4);
    const TileMap& map = em.tile_map;
    const int sc = static_cast<int>(floor.spawn_x) / map.tile_size;
    const int sr = static_cast<int>(floor.spawn_y) / map.tile_size;
    std::string out = "\nspawn tile (" + std::to_string(sc) + "," + std::to_string(sr) + ")\n";
    for (int r = 0; r < map.height; ++r)
    {
        for (int c = 0; c < map.width; ++c)
        {
            char ch = map.tiles[static_cast<std::size_t>(r * map.width + c)].walkable ? '.' : '#';
            if (c == sc && r == sr)
                ch = '@';
            out += ch;
        }
        out += '\n';
    }
    WARN(out);
}

TEST_CASE("spawner markers keep their distances", "[floorgen]")
{
    // The rule under test is the one in config -- read the real numbers rather than repeating
    // them here to drift.
    std::ifstream in("config/floor.json");
    REQUIRE(in.good());
    const nlohmann::json j = nlohmann::json::parse(in);
    const auto& sm = j.at("spaced_markers");
    const std::string types = sm.at("types");
    const float ts = static_cast<float>(j.at("tile_size").get<int>());
    const float minSpawn = sm.at("min_from_spawn_tiles").get<float>() * ts;

    for (unsigned seed = 1; seed <= 200; ++seed)
    {
        EntityManager em;
        const floorgen::Floor floor =
            floorgen::generate(em, "config/floor.json", "config/rooms", seed);
        INFO("seed " << seed);
        REQUIRE(floor.ok);

        std::vector<const floorgen::Marker*> spawners;
        for (const auto& m : floor.markers)
            if (types.find(m.type) != std::string::npos)
                spawners.push_back(&m);
        // THE GUARANTEE: every accepted floor seats at least min_count spawners, because one
        // bearing is campable. (Generation rerolls layouts until this holds.)
        const int minCount = sm.value("min_count", 2);
        CHECK(static_cast<int>(spawners.size()) >= minCount);
        if (spawners.size() < 2)
            continue;
        for (std::size_t a = 0; a < spawners.size(); ++a)
        {
            const float dsx = spawners[a]->x - floor.spawn_x;
            const float dsy = spawners[a]->y - floor.spawn_y;
            CHECK(dsx * dsx + dsy * dsy >= minSpawn * minSpawn);
            for (std::size_t b = a + 1; b < spawners.size(); ++b)
            {
                const float dx = spawners[a]->x - spawners[b]->x;
                const float dy = spawners[a]->y - spawners[b]->y;
                // The apart rule bends (halving) to reach min_count, but never below one tile.
                CHECK(dx * dx + dy * dy >= ts * ts);
            }
        }
    }
}
