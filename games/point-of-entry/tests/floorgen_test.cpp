#include "FloorGen.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/WaveSystem.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <queue>
#include <vector>

#include <catch2/catch_approx.hpp>
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

TEST_CASE("every seep and creature file parses and keeps its promises", "[bestiary]")
{
    // The bestiary is data, so a typo in a file is a content bug the compiler cannot see. The
    // seep files are the roster now: every kind the floor can open must parse, and every
    // creature any of them names must exist, parse, name real art, and carry sane numbers.
    std::ifstream fin("config/floor.json");
    REQUIRE(fin.good());
    const nlohmann::json floorCfg = nlohmann::json::parse(fin);
    std::vector<std::string> creaturePaths;
    const auto kinds = floorCfg.value("seep_types", nlohmann::json::array());
    REQUIRE(!kinds.empty());
    for (const auto& kind : kinds)
    {
        INFO(kind.value("seep", std::string{}));
        CHECK(kind.value("weight", 0) > 0);
        std::ifstream sf(kind.value("seep", std::string{}));
        REQUIRE(sf.good());
        const nlohmann::json sj = nlohmann::json::parse(sf, nullptr, false);
        REQUIRE_FALSE(sj.is_discarded());
        const auto fauna = sj.value("creatures", nlohmann::json::array());
        CHECK(!fauna.empty()); // a hole nothing comes through is set dressing, not a seep
        for (const auto& entry : fauna)
        {
            CHECK(entry.value("weight", 0) > 0);
            creaturePaths.push_back(entry.value("creature", std::string{}));
        }
    }
    for (const auto& path : creaturePaths)
    {
        INFO(path);
        std::ifstream f(path);
        REQUIRE(f.good());
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        REQUIRE_FALSE(j.is_discarded());
        const auto sheet = j.value("sheet", nlohmann::json::object());
        CHECK(sheet.value("resistance", 0) > 0);
        CHECK(sheet.value("defensiveness", 0) > 0);
        CHECK(sheet.value("dispersal", 0) > 0);
        const auto base = j.value("base", nlohmann::json::object());
        CHECK(base.value("hp", 0) > 0);
        CHECK(base.value("power", 0.0f) > 0.0f);
        CHECK(base.value("speed", 0.0f) > 0.0f);
        CHECK(base.value("xp", 0) > 0);
        std::ifstream art(j.value("sprite", std::string{}));
        CHECK(art.good()); // the drawing it names must exist
    }
}

TEST_CASE("what emerges can actually move", "[bestiary]")
{
    // A rewrite of the emergence code once dropped Velocity, leaving every creature a statue --
    // the systems that move things view <Transform, Velocity, Vermin>, and an entity missing
    // any of them silently falls out of the world's attention. This pins the component recipe
    // by running the real machinery: begin an assault, tick until something surfaces, and
    // demand it carries everything the movement pipeline needs.
    EntityManager em;
    em.tile_map.tile_size = 32;
    em.tile_map.width = 10;
    em.tile_map.height = 10;
    em.tile_map.tiles.assign(100, TileMap::Tile{0, true});

    swarm::begin("config/swarm.json",
                 {swarm::Seep{160.0f, 160.0f, "config/seeps/foundation_crack.json"}}, 0, {}, {},
                 {true});
    for (int i = 0; i < 600 && em.registry().view<Vermin>().size() == 0; ++i)
        swarm::update(em, 0.016f);
    REQUIRE(em.registry().view<Vermin>().size() > 0);
    for (const auto e : em.registry().view<Vermin>())
    {
        CHECK(em.registry().all_of<Velocity>(e));
        CHECK(em.registry().all_of<Transform>(e));
        CHECK(em.registry().all_of<Health>(e));
        CHECK(em.registry().all_of<Worth>(e));
        CHECK(em.registry().all_of<SeepSource>(e));
        CHECK(em.registry().all_of<Smell>(e));
    }
}

// The derivation contract: a body's numbers come out of its sheet through the bestiary
// formulas and nowhere else. Fixture configs go to a temp dir with the smell range pinned
// (min == max), so every roll is deterministic without reaching into the RNG.

namespace
{

struct BestiaryFixture
{
    std::string swarm;
    std::string creature;
};

// Known constants and a known sheet, chosen for round hand-computed numbers. atSmell > 0
// wires an evolved form (all-ones sheet, base hp 100) behind that threshold.
BestiaryFixture writeBestiaryFixture(int smellMin, int smellMax, int atSmell)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "poe_bestiary_fixture";
    fs::create_directories(dir);

    const fs::path evolvedPath = dir / "evolved.json";
    nlohmann::json evolved = {
        {"sheet", {{"resistance", 1}, {"defensiveness", 1}, {"dispersal", 1}}},
        {"base", {{"hp", 100}, {"power", 50.0}, {"speed", 300.0}, {"xp", 40}}},
    };
    std::ofstream(evolvedPath) << evolved.dump(2);

    const fs::path creaturePath = dir / "creature.json";
    nlohmann::json creature = {
        {"sheet", {{"resistance", 2}, {"defensiveness", 3}, {"dispersal", 4}}},
        {"base", {{"hp", 10}, {"power", 10.0}, {"speed", 100.0}, {"xp", 8}}},
        {"growth", {{"resistance", 1}, {"defensiveness", 0}, {"dispersal", 2}}},
    };
    if (atSmell > 0)
        creature["evolves"] = {{"into", evolvedPath.generic_string()}, {"at_smell", atSmell}};
    std::ofstream(creaturePath) << creature.dump(2);

    const fs::path swarmPath = dir / "swarm.json";
    const nlohmann::json swarmCfg = {
        {"bestiary",
         {{"hp", {{"per_resistance", 2.0}}},
          {"contact",
           {{"per_point", 0.1}, {"defensiveness_weight", 1.0}, {"resistance_weight", 0.5}}},
          {"speed", {{"per_point", 0.1}, {"dispersal_weight", 1.0}, {"defensiveness_weight", 0.5}}},
          {"xp", {{"per_total", 0.25}}},
          {"smell",
           {{"base_min", smellMin},
            {"base_max", smellMax},
            {"min_per_depth", 0},
            {"max_per_depth", 0}}}}},
    };
    std::ofstream(swarmPath) << swarmCfg.dump(2);
    return {swarmPath.generic_string(), creaturePath.generic_string()};
}

void openRoom(EntityManager& em)
{
    em.tile_map.tile_size = 32;
    em.tile_map.width = 10;
    em.tile_map.height = 10;
    em.tile_map.tiles.assign(100, TileMap::Tile{0, true});
}

entt::entity theOneEmerged(EntityManager& em, const BestiaryFixture& fx, int depth)
{
    openRoom(em);
    swarm::begin(fx.swarm, {}, depth);
    swarm::spawnOne(em, fx.creature, 160.0f, 160.0f);
    REQUIRE(em.registry().view<Vermin>().size() == 1);
    return em.registry().view<Vermin>().front();
}

} // namespace

TEST_CASE("derived numbers follow the bestiary formulas", "[bestiary]")
{
    const BestiaryFixture fx = writeBestiaryFixture(0, 0, 0);
    EntityManager em;
    const entt::entity e = theOneEmerged(em, fx, 0);
    // sheet {2,3,4}: hp = 10 + 2*2; contact = 10*(1 + 0.1*(3 + 2*0.5));
    // speed = 100*(1 + 0.1*(4 + 3*0.5)); xp = 8*(1 + 0.25*(9-3)).
    CHECK(em.registry().get<Health>(e).max == 14);
    CHECK(em.registry().get<Vermin>(e).contact_damage == Catch::Approx(14.0f));
    CHECK(em.registry().get<Vermin>(e).speed == Catch::Approx(155.0f));
    CHECK(em.registry().get<Worth>(e).xp == 20);
    CHECK(em.registry().get<Smell>(e).amount == 0);
}

TEST_CASE("depth deepens a species along its growth spread", "[bestiary]")
{
    const BestiaryFixture fx = writeBestiaryFixture(0, 0, 0);
    EntityManager em;
    const entt::entity e = theOneEmerged(em, fx, 2);
    // Two depths of growth {1,0,2} make the sheet {4,3,8}; everything re-derives from that.
    CHECK(em.registry().get<Health>(e).max == 18);
    CHECK(em.registry().get<Vermin>(e).contact_damage == Catch::Approx(15.0f));
    CHECK(em.registry().get<Vermin>(e).speed == Catch::Approx(195.0f));
    CHECK(em.registry().get<Worth>(e).xp == 32);
}

TEST_CASE("smell multiplies everything derived", "[bestiary]")
{
    const BestiaryFixture fx = writeBestiaryFixture(50, 50, 0);
    EntityManager em;
    const entt::entity e = theOneEmerged(em, fx, 0);
    CHECK(em.registry().get<Smell>(e).amount == 50);
    CHECK(em.registry().get<Health>(e).max == 21);
    CHECK(em.registry().get<Vermin>(e).contact_damage == Catch::Approx(21.0f));
    CHECK(em.registry().get<Vermin>(e).speed == Catch::Approx(232.5f));
    CHECK(em.registry().get<Worth>(e).xp == 30);
}

TEST_CASE("a hot enough roll surfaces the evolved form", "[bestiary]")
{
    const BestiaryFixture fx = writeBestiaryFixture(50, 50, 40);
    EntityManager em;
    const entt::entity e = theOneEmerged(em, fx, 0);
    // The evolved body's numbers, re-rolled smell included (pinned range rolls 50 again).
    CHECK(em.registry().get<Health>(e).max == 153);
    CHECK(em.registry().get<Smell>(e).amount == 50);
}

TEST_CASE("a cool roll stays the base form", "[bestiary]")
{
    const BestiaryFixture fx = writeBestiaryFixture(30, 30, 40);
    EntityManager em;
    const entt::entity e = theOneEmerged(em, fx, 0);
    CHECK(em.registry().get<Smell>(e).amount == 30);
    CHECK(em.registry().get<Health>(e).max == 18); // lround(14 * 1.3) -- the base body, warmed
}
