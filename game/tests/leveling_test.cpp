#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/LevelingSystem.h"
#include "test_helpers.h"

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
    emplaceGameConfigs(em);

    const auto e = makeCharacter(em, 5, 5, 5, 5);
    LevelingSystem::applyInitialDerivations(em);

    REQUIRE(em.registry().all_of<Health>(e));
    const auto& h = em.registry().get<Health>(e);

    // No Body → fallback hp.base=5, no Experience → level=1. maxHP = 5 + 15*5 + 5*1 = 85
    REQUIRE(h.max == 85);
    REQUIRE(h.current == h.max);
}

TEST_CASE("applyInitialDerivations — higher END gives higher max HP", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);

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
    emplaceGameConfigs(em);

    // Wall-like entity: no Stats component.
    const auto wall = em.create();
    LevelingSystem::applyInitialDerivations(em);

    // Should not have Health added by derivations.
    REQUIRE_FALSE(em.registry().all_of<Health>(wall));
}

TEST_CASE("applyInitialDerivations — entity with existing Health gets max overridden", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    const auto e = em.create();
    em.registry().emplace<Stats>(e, Stats{5, 5, 5, 5});
    em.registry().emplace<Health>(e, Health{999, 999}); // stale value

    LevelingSystem::applyInitialDerivations(em);

    const auto& h = em.registry().get<Health>(e);
    // Linear HP: base(5) + scale(15)*END(5) + level_scale(5)*level(1) = 85
    REQUIRE(h.max == 85);
    REQUIRE(h.current == 85);
}

TEST_CASE("applyInitialDerivations — sets xp_to_next on Experience", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    // xp_to_next = xp_base * (level + xp_offset)^exponent = 0.069 * 8^3.5 = 99
    const auto& exp = em.registry().get<Experience>(e);
    REQUIRE(exp.xp_to_next == 99);
}

// ---------------------------------------------------------------------------
// XP threshold formula
// ---------------------------------------------------------------------------

TEST_CASE("XP threshold — level 1 requires 99 XP", "[leveling]")
{
    // xpToNext = xp_base * (level + xp_offset) ^ xp_exponent = 0.069 * 8^3.5 = 99
    const FormulaConfig f;
    const int threshold =
        static_cast<int>(f.leveling.xp_base * std::pow(static_cast<float>(1) + f.leveling.xp_offset,
                                                       f.leveling.xp_exponent));
    REQUIRE(threshold == 99);
}

TEST_CASE("XP threshold — level 2 threshold is higher than level 1", "[leveling]")
{
    const FormulaConfig f;
    const int t1 = static_cast<int>(f.leveling.xp_base *
                                    std::pow(1.0f + f.leveling.xp_offset, f.leveling.xp_exponent));
    const int t2 = static_cast<int>(f.leveling.xp_base *
                                    std::pow(2.0f + f.leveling.xp_offset, f.leveling.xp_exponent));
    REQUIRE(t2 > t1);
}

// ---------------------------------------------------------------------------
// Level-up via LevelingSystem::update
// ---------------------------------------------------------------------------

TEST_CASE("Level up — exactly hitting threshold increments level", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    exp.current_xp = 99; // exactly level 1 threshold

    LevelingSystem::update(em);

    REQUIRE(exp.level == 2);
    REQUIRE(exp.current_xp == 0); // 99 - 99 = 0 carried over
    REQUIRE(exp.stat_points == 1);
}

TEST_CASE("Level up — XP overflow carries over to next level", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    exp.current_xp = 130; // 31 excess after level-up (99 threshold)

    LevelingSystem::update(em);

    REQUIRE(exp.level == 2);
    REQUIRE(exp.current_xp == 31);
    REQUIRE(exp.stat_points == 1);
}

TEST_CASE("Level up — multiple levels in one update (huge XP gain)", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    auto& exp = em.registry().get<Experience>(e);
    // Level 1→2: 99 XP, Level 2→3: 0.069*9^3.5 = 150 XP.
    // Give enough to clear both: 99 + 150 + 1 = 250
    exp.current_xp = 250;

    LevelingSystem::update(em);

    REQUIRE(exp.level == 3);
    REQUIRE(exp.stat_points == 2);
    REQUIRE(exp.current_xp >= 0);
    REQUIRE(exp.current_xp < exp.xp_to_next);
}

TEST_CASE("Level up — not enough XP: no level change", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
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
// Stat allocation via PlayerActions flags
// ---------------------------------------------------------------------------

TEST_CASE("Stat allocation — alloc_str increments STR and spends a point", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    // Give the player some stat points to spend.
    em.registry().get<Experience>(e).stat_points = 1;

    // Attach a PlayerActions component (required by allocation path).
    em.registry().emplace<PlayerActions>(e);
    em.registry().get<PlayerActions>(e).alloc_str = true;

    LevelingSystem::update(em);

    const auto& stats = em.registry().get<Stats>(e);
    const auto& exp = em.registry().get<Experience>(e);
    REQUIRE(stats.str == 6);
    REQUIRE(exp.stat_points == 0);
}

TEST_CASE("Stat allocation — alloc_end increases Health.max", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    const int prevMax = em.registry().get<Health>(e).max;
    em.registry().get<Experience>(e).stat_points = 1;
    em.registry().emplace<PlayerActions>(e);
    em.registry().get<PlayerActions>(e).alloc_end = true;

    LevelingSystem::update(em);

    const int newMax = em.registry().get<Health>(e).max;
    REQUIRE(newMax > prevMax);
    REQUIRE(em.registry().get<Stats>(e).end == 6);
}

TEST_CASE("Stat allocation — no points available: stat unchanged", "[leveling]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    const auto e = makeCharacter(em);
    LevelingSystem::applyInitialDerivations(em);

    em.registry().emplace<PlayerActions>(e);
    em.registry().get<PlayerActions>(e).alloc_dex = true;
    // stat_points stays 0 (not set)

    LevelingSystem::update(em);

    REQUIRE(em.registry().get<Stats>(e).dex == 5); // unchanged
}
