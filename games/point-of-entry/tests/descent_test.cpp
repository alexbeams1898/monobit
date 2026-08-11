#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/WaveSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <entt/entt.hpp>

// The persistence primitive under the descent tree: a hole marked cleared
// starts SPENT -- no waves, immediately diggable -- while its neighbours
// press as normal. (The tree's traversal itself needs GL for floor building
// and is integration-tested by running the game.)

namespace
{
std::vector<swarm::Seep> twoSeeps()
{
    return {swarm::Seep{160.0f, 160.0f, "config/seeps/foundation_crack.json"},
            swarm::Seep{480.0f, 160.0f, "config/seeps/gnaw_hole.json"}};
}
} // namespace

TEST_CASE("a pre-cleared hole starts spent; its neighbour presses", "[descent]")
{
    EntityManager em;
    swarm::begin("config/swarm.json", twoSeeps(), 0, {true, false});

    CHECK(swarm::seepCleared(em, 0));
    CHECK_FALSE(swarm::seepCleared(em, 1));
    CHECK(swarm::phase() != swarm::Phase::Cleared);

    // Long enough for emergence: everything that surfaces belongs to the
    // uncleared hole -- the spent one never speaks again.
    for (int i = 0; i < 600; ++i)
        swarm::update(em, 1.0f / 60.0f);
    int fromSpent = 0;
    int fromLive = 0;
    for (const auto [e, src] : em.registry().view<SeepSource>().each())
    {
        if (src.index == 0)
            ++fromSpent;
        if (src.index == 1)
            ++fromLive;
    }
    CHECK(fromSpent == 0);
    CHECK(fromLive > 0);
}

TEST_CASE("every hole pre-cleared is a floor already at rest", "[descent]")
{
    EntityManager em;
    swarm::begin("config/swarm.json", twoSeeps(), 0, {true, true});
    CHECK(swarm::phase() == swarm::Phase::Cleared);
    swarm::update(em, 1.0f);
    CHECK(em.registry().view<Vermin>().size() == 0);
}
