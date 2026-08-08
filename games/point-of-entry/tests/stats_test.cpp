#include "Stats.h"
#include "Tools.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

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
