#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ops/ZoneUtils.h"
#include "systems/DescentSystem.h"
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

// A LEAK IS A QUESTION ABOUT THE FLOOR BEHIND IT, never a latch on the hole. Only the
// no-floor case runs headless: standing on a real floor needs a window, so the rest --
// undug leaks, finished goes quiet -- is covered by playing it.
TEST_CASE("a way down with no floor under it stays shut")
{
    EntityManager em;
    descent::reset();

    DigSite site;
    site.hole = 0; // every way down is a hole of some floor
    site.trickle = "config/creatures/ant.json";
    const entt::entity e = em.registry().create();
    em.registry().emplace<DigSite>(e, site);

    descent::refreshLeaks(em);
    REQUIRE_FALSE(em.registry().get<DigSite>(e).leaking);

    // Nor does a hole numbered past the end of the floor he is standing in.
    em.registry().get<DigSite>(e).hole = 99;
    descent::refreshLeaks(em);
    REQUIRE_FALSE(em.registry().get<DigSite>(e).leaking);
}

// THE WORK STATE: one answer per tick, and everything that looks different between
// exterminating and not asks it. (Generated space is the other half of the rule; it needs a
// built floor, so it is covered by playing.)
TEST_CASE("the work state follows the leak")
{
    EntityManager em;
    descent::reset(); // not standing in a dug floor: the authored rules apply
    zone::reset();
    REQUIRE_FALSE(zone::combat());

    const entt::entity e = em.registry().create();
    DigSite site;
    site.trickle = "config/creatures/ant.json";
    em.registry().emplace<DigSite>(e, site);

    SECTION("a quiet way down in an authored room is not the trade's ground")
    {
        em.registry().get<DigSite>(e).leaking = false; // everything below it is finished
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }

    SECTION("a leaking one is")
    {
        em.registry().get<DigSite>(e).leaking = true;
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
    }

    SECTION("a hole with nothing to send up is not, however open it is")
    {
        em.registry().get<DigSite>(e).leaking = true;
        em.registry().get<DigSite>(e).trickle.clear();
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }
}

TEST_CASE("the changeover waits for something to see")
{
    EntityManager em;
    descent::reset();
    zone::reset();
    const entt::entity e = em.registry().create();
    DigSite site;
    site.trickle = "config/creatures/ant.json";
    site.leaking = true;
    em.registry().emplace<DigSite>(e, site);

    SECTION("a flip starts the changeover over again")
    {
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
        REQUIRE(zone::settle() == 1.0f); // a whole second: long since arrived

        em.registry().get<DigSite>(e).leaking = false;
        zone::update(em, 0.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
        REQUIRE(zone::settle() == 0.0f);
    }

    SECTION("it is held at the start while a curtain is down, then plays")
    {
        zone::update(em, 1.0f, /*cut=*/true);
        REQUIRE(zone::combat()); // the fact does NOT wait for the curtain
        REQUIRE(zone::settle() == 0.0f);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::settle() == 1.0f);
    }
}

TEST_CASE("the work state asks only whether anything can reach him")
{
    EntityManager em;
    descent::reset();
    zone::reset();

    SECTION("a quiet room with nothing in it is not the trade's ground")
    {
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }

    SECTION("one of the swarm still on its feet is enough, wherever he is")
    {
        const entt::entity v = em.registry().create();
        em.registry().emplace<Vermin>(v);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE(zone::combat());
    }

    SECTION("one already dying is not")
    {
        const entt::entity v = em.registry().create();
        em.registry().emplace<Vermin>(v);
        em.registry().emplace<Dying>(v);
        zone::update(em, 1.0f, /*cut=*/false);
        REQUIRE_FALSE(zone::combat());
    }
}
