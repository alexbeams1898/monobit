#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/LevelingSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// LevelingSystem tests — no window, no GPU, no SDL required.
//
// Covers: initial HP derivation, XP-to-level-up threshold, level-up logic,
// XP overflow carry-over, and stat point allocation.
// ---------------------------------------------------------------------------

// Helper: build a minimal player-like entity with Stats + Experience.
static entt::entity makeCharacter(EntityManager& em, int str = 5, int dex = 5, int end = 5,
                                  int lck = 5)
{
    auto e = em.create();
    em.registry().emplace<Stats>(e, Stats{str, dex, end, lck});
    em.registry().emplace<Experience>(e, Experience{});
    return e;
}

// ---------------------------------------------------------------------------
// HP derivation (applyInitialDerivations)
// ---------------------------------------------------------------------------

TEST_CASE("applyInitialDerivations — derives Health from END for stat entity", "[leveling]")
{
    EntityManager em;
    // formulas keep defaults (match formulas.json values)

    const auto e = makeCharacter(em, 5, 5, 5, 5);
    LevelingSystem::applyInitialDerivations(em);

    REQUIRE(em.registry().all_of<Health>(e));
    const auto& h = em.registry().get<Health>(e);

    // maxHP = 5 + floor(100 * log(6)) = 5 + 179 = 184
    REQUIRE(h.max == 184);
    REQUIRE(h.current == h.max);
}

TEST_CASE("applyInitialDerivations — higher END gives higher max HP", "[leveling]")
{
    EntityManager em;

    const auto eLow = makeCharacter(em, 5, 5, 1, 5);   // end=1
    const auto eHigh = makeCharacter(em, 5, 5, 10, 5); // end=10
    LevelingSystem::applyInitialDerivations(em);

    const int lowHP = em.registry().get<Health>(eLow).max;
    const int highHP = em.registry().get<Health>(eHigh).max;
    REQUIRE(highHP > lowHP);
}

TEST_CASE("applyInitialDerivations — entity without Stats is not given Health", "[leveling]")
{
    EntityManager em;

    // Wall-like entity: no Stats component.
    const auto wall = em.create();
    LevelingSystem::applyInitialDerivations(em);

    // Should not have Health added by derivations.
    REQUIRE_FALSE(em.registry().all_of<Health>(wall));
}

TEST_CASE("applyInitialDerivations — entity with existing Health gets max overridden", "[leveling]")
{
    EntityManager em;

    const auto e = em.create();
    em.registry().emplace<Stats>(e, Stats{5, 5, 5, 5});
    em.registry().emplace<Health>(e, Health{999, 999}); // stale value

    LevelingSystem::applyInitialDerivations(em);

    const auto& h = em.registry().get<Health>(e);
    REQUIRE(h.max == 184); // overridden by formula
    REQUIRE(h.current == 184);
}

TEST_CASE("applyInitialDerivations — sets xp_to_next on Experience", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    // xp_to_next = xp_base * level^exponent = 100 * 1^1.5 = 100
    const auto& exp = em.registry().get<Experience>(e);
    REQUIRE(exp.xp_to_next == 100);
}

// ---------------------------------------------------------------------------
// XP threshold formula
// ---------------------------------------------------------------------------

TEST_CASE("XP threshold — level 1 requires 100 XP", "[leveling]")
{
    // xpToNext = xp_base * level ^ xp_exponent = 100 * 1^1.5 = 100
    const FormulaConfig f;
    const int threshold =
        static_cast<int>(f.leveling.xp_base * std::pow(1.0f, f.leveling.xp_exponent));
    REQUIRE(threshold == 100);
}

TEST_CASE("XP threshold — level 2 threshold is higher than level 1", "[leveling]")
{
    const FormulaConfig f;
    const int t1 = static_cast<int>(f.leveling.xp_base * std::pow(1.0f, f.leveling.xp_exponent));
    const int t2 = static_cast<int>(f.leveling.xp_base * std::pow(2.0f, f.leveling.xp_exponent));
    REQUIRE(t2 > t1);
}

// ---------------------------------------------------------------------------
// Level-up via LevelingSystem::update
// ---------------------------------------------------------------------------

TEST_CASE("Level up — exactly hitting threshold increments level", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    exp.current_xp = 100; // exactly level 1 threshold

    LevelingSystem::update(em);

    REQUIRE(exp.level == 2);
    REQUIRE(exp.current_xp == 0); // 100 - 100 = 0 carried over
    REQUIRE(exp.stat_points == 1);
}

TEST_CASE("Level up — XP overflow carries over to next level", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    exp.current_xp = 150; // 50 excess after level-up

    LevelingSystem::update(em);

    REQUIRE(exp.level == 2);
    REQUIRE(exp.current_xp == 50);
    REQUIRE(exp.stat_points == 1);
}

TEST_CASE("Level up — multiple levels in one update (huge XP gain)", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    // Level 1→2: 100 XP, Level 2→3: 100*2^1.5 ≈ 282 XP.
    // Give enough to clear both: 100 + 282 + 1 = 383
    exp.current_xp = 383;

    LevelingSystem::update(em);

    REQUIRE(exp.level == 3);
    REQUIRE(exp.stat_points == 2);
    REQUIRE(exp.current_xp >= 0);
    REQUIRE(exp.current_xp < exp.xp_to_next);
}

TEST_CASE("Level up — not enough XP: no level change", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    exp.current_xp = 50; // below threshold of 100

    LevelingSystem::update(em);

    REQUIRE(exp.level == 1);
    REQUIRE(exp.stat_points == 0);
    REQUIRE(exp.current_xp == 50);
}

// ---------------------------------------------------------------------------
// Stat allocation via Input flags
// ---------------------------------------------------------------------------

TEST_CASE("Stat allocation — alloc_str increments STR and spends a point", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    // Give the player some stat points to spend.
    em.registry().get<Experience>(e).stat_points = 1;

    // Attach an Input component (required by allocation path).
    em.registry().emplace<Input>(e);
    em.registry().get<Input>(e).alloc_str = true;

    LevelingSystem::update(em);

    const auto& stats = em.registry().get<Stats>(e);
    const auto& exp = em.registry().get<Experience>(e);
    REQUIRE(stats.str == 6);
    REQUIRE(exp.stat_points == 0);
}

TEST_CASE("Stat allocation — alloc_end increases Health.max", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    const int prevMax = em.registry().get<Health>(e).max;
    em.registry().get<Experience>(e).stat_points = 1;
    em.registry().emplace<Input>(e);
    em.registry().get<Input>(e).alloc_end = true;

    LevelingSystem::update(em);

    const int newMax = em.registry().get<Health>(e).max;
    REQUIRE(newMax > prevMax);
    REQUIRE(em.registry().get<Stats>(e).end == 6);
}

TEST_CASE("Stat allocation — no points available: stat unchanged", "[leveling]")
{
    EntityManager em;
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    em.registry().emplace<Input>(e);
    em.registry().get<Input>(e).alloc_dex = true;
    // stat_points stays 0 (not set)

    LevelingSystem::update(em);

    REQUIRE(em.registry().get<Stats>(e).dex == 5); // unchanged
}
