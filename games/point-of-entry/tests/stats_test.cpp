#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/CombatSystem.h"
#include "systems/DescentSystem.h"
#include "systems/PlayerSystem.h"
#include "systems/RewardSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// The derivation promises: level is spent points and nothing else, bodies come out of the
// sheet, scaling rewards the right discipline, and a re-derive never heals for free.

TEST_CASE("level is points spent, nothing else", "[stats]")
{
    Stats s; // all ones
    CHECK(stats::level(s) == 1);
    s.chemical = 3;
    s.endurance = 2;
    CHECK(stats::level(s) == 4); // 2 + 1 points above baseline, +1
}

TEST_CASE("the body derives from the sheet", "[stats]")
{
    Stats a;
    Stats b;
    b.endurance = 5;
    CHECK(stats::maxHealth(b) > stats::maxHealth(a));
    CHECK(stats::maxStamina(b) > stats::maxStamina(a));
    // Physical feeds health a little, stamina not at all.
    Stats c;
    c.physical = 5;
    CHECK(stats::maxHealth(c) > stats::maxHealth(a));
    CHECK(stats::maxStamina(c) == Catch::Approx(stats::maxStamina(a)));
}

TEST_CASE("defense rises with level, physical and endurance", "[stats]")
{
    Stats a;
    Stats b;
    b.physical = 4;
    b.endurance = 4;
    CHECK(stats::defense(b) > stats::defense(a));
    // A sheet levelled into unrelated stats still gains a LITTLE defense -- the level term.
    Stats c;
    c.inspection = 10;
    CHECK(stats::defense(c) >= stats::defense(a));
}

TEST_CASE("a tool rewards its own discipline only", "[stats]")
{
    tools::Tool wand;
    wand.damage = 10.0f;
    wand.scale_chemical = 1.0f;

    Stats chemist;
    chemist.chemical = 6;
    Stats bruiser;
    bruiser.physical = 6;

    CHECK(tools::damageOf(wand, chemist) > wand.damage);
    CHECK(tools::damageOf(wand, bruiser) == Catch::Approx(wand.damage));

    // No grades at all: the sheet is ignored entirely.
    tools::Tool plain;
    plain.damage = 10.0f;
    CHECK(tools::damageOf(plain, chemist) == Catch::Approx(plain.damage));
}

TEST_CASE("a re-derive keeps the fraction rather than healing", "[stats]")
{
    EntityManager em;
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Stats>(e, Stats{});
    stats::applyDerivations(em, e);

    auto& hp = reg.get<Health>(e);
    hp.current = hp.max / 2;
    const float fracBefore = static_cast<float>(hp.current) / static_cast<float>(hp.max);

    reg.get<Stats>(e).endurance = 8;
    stats::applyDerivations(em, e);
    const auto& hp2 = reg.get<Health>(e);
    const float fracAfter = static_cast<float>(hp2.current) / static_cast<float>(hp2.max);
    CHECK(fracAfter == Catch::Approx(fracBefore).margin(0.02f));
    CHECK(hp2.max > 80); // it did actually grow
}

TEST_CASE("the guard pays in stamina, breaks when it cannot, and stands aside when down", "[block]")
{
    stats::load("config/stats.json");
    EntityManager em;
    auto& reg = em.registry();
    const entt::entity p = reg.create();
    reg.emplace<Transform>(p, Transform{0.0f, 0.0f});
    reg.emplace<Stamina>(p, Stamina{100.0f, 100.0f, 0.0f});
    player::bind(p);

    // Guard up with a full bar: the hit is absorbed and the bar pays for it.
    CHECK(tools::absorbWithGuard(em, 10, true) == 0);
    const float afterFirst = reg.get<Stamina>(p).current;
    CHECK(afterFirst < 100.0f);
    CHECK(reg.get<Stamina>(p).recovery_timer > 0.0f);

    // A bar too empty to pay: the guard breaks -- the rest of the bar is
    // spent and only a FRACTION of the hit gets through, because the arm is
    // still up. Dropping the guard is the only way to eat a hit whole.
    reg.get<Stamina>(p).current = 1.0f;
    const int through = tools::absorbWithGuard(em, 10, true);
    CHECK(through > 0);
    CHECK(through < 10);
    CHECK(reg.get<Stamina>(p).current == 0.0f);

    // Guard down: the hit passes untouched, whatever the bar holds.
    reg.get<Stamina>(p).current = 100.0f;
    CHECK(tools::absorbWithGuard(em, 10, false) == 10);
}

// THE RATE THE JOB PAYS AT. One front is the careful way and pays plainly; each one beyond it
// is a deliberate risk, and this is what the risk buys. (What counts as a front needs a floor
// underfoot, so the count itself is covered by playing; this pins the arithmetic on top of it.)
TEST_CASE("holding more fronts pays more per kill", "[reward]")
{
    REQUIRE(stats::load("config/stats.json"));
    const float per = stats::frontBonus();
    REQUIRE(per > 0.0f);

    // Standing on no floor at all: nothing is open, so the work pays its plain rate.
    descent::reset();
    REQUIRE(reward::rate() == 1.0f);

    // The shape the credit applies, independent of where the count comes from.
    const auto rateFor = [&](int fronts)
    { return fronts <= 1 ? 1.0f : 1.0f + static_cast<float>(fronts - 1) * per; };
    CHECK(rateFor(0) == 1.0f);
    CHECK(rateFor(1) == 1.0f);
    CHECK(rateFor(2) == Catch::Approx(1.0f + per));
    CHECK(rateFor(4) == Catch::Approx(1.0f + per * 3.0f));
    // Each extra front is worth the same as the last -- no runaway on a floor full of holes.
    CHECK(rateFor(4) - rateFor(3) == Catch::Approx(rateFor(3) - rateFor(2)));
}
