#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"

#include <catch2/catch_test_macros.hpp>
#include <entt/entt.hpp>

// The loop's promises: worth is carried rather than converted, points are sold only at a rest
// spot, the price climbs with the level already bought, and buying is never a heal.

namespace
{
entt::entity makePlayer(EntityManager& em)
{
    auto& reg = em.registry();
    const entt::entity p = reg.create();
    reg.emplace<Transform>(p, Transform{});
    reg.emplace<Stats>(p, Stats{});
    stats::applyDerivations(em, p);
    player::bind(p);
    return p;
}

entt::entity makeRestSpot(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{x, y});
    reg.emplace<RestSpot>(e, RestSpot{40.0f});
    return e;
}
} // namespace

TEST_CASE("a kill pays instantly and levels nothing by itself", "[reward]")
{
    EntityManager em;
    makePlayer(em);
    reward::credit(em, 25);
    CHECK(reward::banked(em) == 25);
    // The sheet is untouched until something is bought.
    CHECK(stats::level(em.registry().get<Stats>(player::entity())) == 1);
}

TEST_CASE("points are sold only at the rest spot", "[reward]")
{
    EntityManager em;
    const entt::entity p = makePlayer(em);
    reward::credit(em, 10000);

    // Away from any spot: the pocket is full and the answer is still no.
    CHECK_FALSE(reward::spend(em, 0));

    makeRestSpot(em, 10.0f, 0.0f); // within its radius
    CHECK(reward::spend(em, 0));
    CHECK(em.registry().get<Stats>(p).chemical == 2);
}

TEST_CASE("the price climbs with the level already bought", "[reward]")
{
    EntityManager em;
    makePlayer(em);
    makeRestSpot(em, 0.0f, 0.0f);
    reward::credit(em, 100000);

    const int first = reward::costOfNext(em);
    REQUIRE(reward::spend(em, 3));
    CHECK(reward::costOfNext(em) > first);
}

TEST_CASE("buying is never a heal", "[reward]")
{
    EntityManager em;
    const entt::entity p = makePlayer(em);
    makeRestSpot(em, 0.0f, 0.0f);
    reward::credit(em, 100000);

    auto& hp = em.registry().get<Health>(p);
    hp.current = hp.max / 2;
    REQUIRE(reward::spend(em, 3)); // endurance: max grows
    const auto& hp2 = em.registry().get<Health>(p);
    CHECK(hp2.max > 80);
    CHECK(hp2.current < hp2.max); // grew with the fraction, did not snap to full
}

TEST_CASE("an empty pocket buys nothing", "[reward]")
{
    EntityManager em;
    const entt::entity p = makePlayer(em);
    makeRestSpot(em, 0.0f, 0.0f);
    CHECK_FALSE(reward::spend(em, 0));
    CHECK(em.registry().get<Stats>(p).chemical == 1);
}
