#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"
#include "systems/ThermosSystem.h"

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

TEST_CASE("the pocket survives death", "[reward]")
{
    // Death resets the floor, not the ledger -- what was earned is carried through. The reset
    // itself lives in the shell; the promise testable here is that nothing in the reward
    // machinery ties the pocket to being alive.
    EntityManager em;
    const entt::entity p = makePlayer(em);
    reward::credit(em, 300);
    auto& hp = em.registry().get<Health>(p);
    hp.current = 0; // dead by any system's standard
    CHECK(reward::banked(em) == 300);
}

TEST_CASE("the thermos rations, refuses waste, and refills on rest", "[thermos]")
{
    EntityManager em;
    const entt::entity p = makePlayer(em);
    thermos::load("config/stats.json");
    REQUIRE(thermos::sipsLeft() == thermos::sipsMax());

    // A sip at full health and full stamina is refused -- the flask does not waste.
    CHECK_FALSE(thermos::sip(em));
    CHECK(thermos::sipsLeft() == thermos::sipsMax());

    // Hurt, a sip heals and costs a charge.
    auto& hp = em.registry().get<Health>(p);
    hp.current = 1;
    CHECK(thermos::sip(em));
    CHECK(em.registry().get<Health>(p).current > 1);
    CHECK(thermos::sipsLeft() == thermos::sipsMax() - 1);

    // Drain it dry: an empty flask does nothing however hurt he is.
    for (int i = 0; i < 20; ++i)
    {
        em.registry().get<Health>(p).current = 1;
        thermos::sip(em);
    }
    em.registry().get<Health>(p).current = 1;
    CHECK_FALSE(thermos::sip(em));

    // Resting refills and mends in one act.
    thermos::rest(em);
    CHECK(thermos::sipsLeft() == thermos::sipsMax());
    CHECK(em.registry().get<Health>(p).current == em.registry().get<Health>(p).max);
}
