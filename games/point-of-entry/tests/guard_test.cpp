#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/CombatSystem.h"
#include "systems/PlayerSystem.h"

#include <catch2/catch_test_macros.hpp>

// WHAT THE GUARD IS FOR: stamina spent instead of health, until there is no stamina left, and
// then a fraction of the hit rather than none of it. Every one of these is arithmetic on the
// sheet, and none of it needs a window -- which is why it is tested here rather than found in
// play at the moment it matters most.
namespace
{
entt::entity aMan(EntityManager& em, float stamina)
{
    const entt::entity p = em.registry().create();
    em.registry().emplace<Health>(p, Health{100, 100});
    em.registry().emplace<Stamina>(p, Stamina{stamina, 100.0f});
    player::bind(p);
    return p;
}
} // namespace

TEST_CASE("a hit not guarded lands whole", "[combat]")
{
    EntityManager em;
    aMan(em, 100.0f);
    CHECK(tools::absorbWithGuard(em, 7, /*guarding=*/false) == 7);
}

TEST_CASE("a guard with stamina behind it takes the hit instead of him", "[combat]")
{
    EntityManager em;
    const entt::entity p = aMan(em, 100.0f);
    REQUIRE(stats::load("config/stats.json"));

    CHECK(tools::absorbWithGuard(em, 10, /*guarding=*/true) == 0);
    const auto& sta = em.registry().get<Stamina>(p);
    CHECK(sta.current < 100.0f);      // it cost him
    CHECK(sta.recovery_timer > 0.0f); // and it costs him the recovery too
}

TEST_CASE("a guard with nothing behind it breaks, and only a fraction gets through", "[combat]")
{
    EntityManager em;
    const entt::entity p = aMan(em, 0.5f); // not enough for anything
    REQUIRE(stats::load("config/stats.json"));

    const int through = tools::absorbWithGuard(em, 10, /*guarding=*/true);
    CHECK(through > 0);  // the guard broke
    CHECK(through < 10); // but the arm was still up
    CHECK(em.registry().get<Stamina>(p).current == 0.0f);
}

// A HIT ALWAYS COSTS SOMETHING. Rounding a small hit through a broken guard down to nothing
// would make a man with an empty bar invulnerable to the weakest thing in the game.
TEST_CASE("even the smallest hit through a broken guard lands", "[combat]")
{
    EntityManager em;
    aMan(em, 0.0f);
    REQUIRE(stats::load("config/stats.json"));
    CHECK(tools::absorbWithGuard(em, 1, /*guarding=*/true) >= 1);
}
