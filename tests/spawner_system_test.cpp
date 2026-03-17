#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/SpawnerSystem.h"

#include <catch2/catch_test_macros.hpp>

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
            if (tag.name == "enemy")
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
        if (tag.name == "enemy")
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
