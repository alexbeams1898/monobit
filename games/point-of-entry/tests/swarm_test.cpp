#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/WaveSystem.h"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// WHAT A DIG COSTS HIM, and the law the whole descent is built on: deeper is worse, by the
// numbers in config/swarm.json rather than by anything written here. These pin the SHAPE of the
// curve -- that it climbs, that a hole's program is finite, that being sealed means silence --
// and deliberately not the values, which are tuning and change.
namespace
{
std::vector<swarm::Hole> oneHole(const char* kind = "config/holes/foundation_crack.json")
{
    return {swarm::Hole{160.0f, 160.0f, kind}};
}

// Run the assault until it settles or the clock runs out, and say how many ticks it took.
int settle(EntityManager& em, int maxTicks = 20000)
{
    for (int i = 0; i < maxTicks; ++i)
    {
        if (swarm::phase() == swarm::Phase::Cleared)
            return i;
        swarm::update(em, 1.0f / 60.0f);
        // Nothing kills them in here, so clear the field by hand: what is under test is the
        // PROGRAM a hole runs, not whether a man can win the fight.
        std::vector<entt::entity> out;
        for (auto e : em.registry().view<Pest>())
            out.push_back(e);
        for (const auto e : out)
        {
            swarm::countKill(em, e);
            em.registry().destroy(e);
        }
    }
    return -1;
}
} // namespace

TEST_CASE("a hole runs a finite program and then is done", "[swarm]")
{
    EntityManager em;
    swarm::begin("config/swarm.json", oneHole(), 0, {false}, {}, {true});
    REQUIRE(swarm::holeWaves(0) > 0);
    const int ticks = settle(em);
    INFO("ticks to clear: " << ticks);
    CHECK(ticks > 0);
    CHECK(swarm::phase() == swarm::Phase::Cleared);
}

TEST_CASE("deeper is worse -- the law of depth, from the config", "[swarm]")
{
    const EntityManager em;
    swarm::begin("config/swarm.json", oneHole(), 0, {false}, {}, {true});
    const int shallowWaves = swarm::holeWaves(0);

    swarm::begin("config/swarm.json", oneHole(), 8, {false}, {}, {true});
    const int deepWaves = swarm::holeWaves(0);

    INFO("waves at depth 0: " << shallowWaves << ", at depth 8: " << deepWaves);
    CHECK(deepWaves > shallowWaves);
}

TEST_CASE("a sealed hole does nothing, and an opened one does", "[swarm]")
{
    EntityManager em;

    SECTION("sealed: nothing surfaces, however long it is left")
    {
        swarm::begin("config/swarm.json", oneHole(), 0, {false}, {}, {false});
        CHECK(swarm::holeSealed(0));
        for (int i = 0; i < 600; ++i)
            swarm::update(em, 1.0f / 60.0f);
        CHECK(em.registry().view<Pest>().empty());
    }

    SECTION("opened: it starts producing")
    {
        swarm::begin("config/swarm.json", oneHole(), 0, {false}, {}, {false});
        swarm::wake(0);
        CHECK_FALSE(swarm::holeSealed(0));
        bool any = false;
        for (int i = 0; i < 1200 && !any; ++i)
        {
            swarm::update(em, 1.0f / 60.0f);
            any = !em.registry().view<Pest>().empty();
        }
        CHECK(any);
    }
}

TEST_CASE("a hole already cleared starts spent", "[swarm]")
{
    const EntityManager em;
    swarm::begin("config/swarm.json", oneHole(), 0, {true}, {}, {true});
    CHECK(swarm::holeCleared(em, 0));
    CHECK(swarm::phase() == swarm::Phase::Cleared);
}

// KILLS ARE REMEMBERED PER HOLE, so a fight resumes where it stopped rather than restarting --
// which is what makes leaving a floor mid-assault cost progress instead of erasing it.
TEST_CASE("a hole resumes its program rather than restarting it", "[swarm]")
{
    EntityManager em;
    swarm::begin("config/swarm.json", oneHole(), 0, {false}, {}, {true});
    for (int i = 0; i < 400; ++i)
        swarm::update(em, 1.0f / 60.0f);

    std::vector<entt::entity> out;
    for (auto e : em.registry().view<Pest>())
        out.push_back(e);
    REQUIRE_FALSE(out.empty());
    for (const auto e : out)
    {
        swarm::countKill(em, e);
        em.registry().destroy(e);
    }
    const int taken = swarm::progress().at(0);
    CHECK(taken > 0);

    // Walk away and come back carrying what was taken: the hole picks up, it does not restart.
    const EntityManager again;
    swarm::begin("config/swarm.json", oneHole(), 0, {false}, {taken}, {true});
    CHECK(swarm::progress().at(0) == taken);
}
