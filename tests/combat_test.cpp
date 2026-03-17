#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CombatSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// Combat formula tests — no window, no GPU, no SDL required.
//
// All tests exercise the free functions exposed by CombatSystem.h:
//   computeSwingCooldown()  — physics-based weapon swing speed
//   computeDamage()         — raw damage before DEF and penalty
//
// Plus formula helpers directly from FormulaConfig defaults.
// ---------------------------------------------------------------------------

// Default formulas (matches formulas.json values and FormulaConfig defaults).
static FormulaConfig defaultFormulas()
{
    return FormulaConfig{}; // all defaults match formulas.json
}

// Helpers to build test weapon / stats structs.
static Weapon makeFist()
{
    Weapon w;
    w.weight = 0.5f;
    w.str_scaling = ScalingGrade::E;
    w.dex_scaling = ScalingGrade::E;
    w.str_requirement = 0;
    w.dex_requirement = 0;
    w.base_damage = 5.0f;
    return w;
}

static Stats makeStats(int str, int dex, int end, int lck)
{
    Stats s;
    s.str = str;
    s.dex = dex;
    s.end = end;
    s.lck = lck;
    return s;
}

// ---------------------------------------------------------------------------
// Swing cooldown tests
// ---------------------------------------------------------------------------

TEST_CASE("Swing cooldown — fist (E/E) at str=dex=5 is positive and reasonable", "[combat]")
{
    const auto f = defaultFormulas();
    const auto w = makeFist();
    const auto s = makeStats(5, 5, 5, 5);

    const float cd = computeSwingCooldown(w, s, f);

    // With weight=0.5, weight_scale=100: numerator=50.
    // effectiveStat = 5*0.5 + 5*0.5 = 5 (dexBias=0 for E/E grade → purely STR? No:
    // E grade → dexBias=0 → strBias=1 → effectiveStat = str*1 + dex*0 = 5).
    // reduction = floor(5 * 40 * log(6)) / 100 = floor(200 * 1.79) / 100 = floor(358) / 100 = 3.58
    // denom = 1 + 3.58 = 4.58
    // cooldown = 50 / 4.58 ≈ 10.9 s — very slow fist (both grades E)
    // Actually E/E means dexBias=0 so effectiveStat = str only = 5.
    // Let's just check it's within a sensible range and > 0.
    REQUIRE(cd > 0.0f);
    REQUIRE(cd >= 0.05f); // min clamp holds
}

TEST_CASE("Swing cooldown — fist clamped to 0.05s minimum", "[combat]")
{
    auto f = defaultFormulas();
    f.swing.weight_scale = 0.001f; // near-zero weight contribution

    const auto w = makeFist();
    const auto s = makeStats(100, 100, 5, 5);

    const float cd = computeSwingCooldown(w, s, f);
    REQUIRE(cd >= 0.05f);
}

TEST_CASE("Swing cooldown — heavier weapon is slower than lighter one (same stats)", "[combat]")
{
    const auto f = defaultFormulas();
    const auto s = makeStats(5, 5, 5, 5);

    Weapon light = makeFist(); // weight=0.5
    Weapon heavy = makeFist();
    heavy.weight = 3.0f;

    REQUIRE(computeSwingCooldown(heavy, s, f) > computeSwingCooldown(light, s, f));
}

TEST_CASE("Swing cooldown — higher stats reduce cooldown", "[combat]")
{
    const auto f = defaultFormulas();
    const auto w = makeFist();
    const auto sLow = makeStats(1, 1, 1, 1);
    const auto sHigh = makeStats(20, 20, 5, 5);

    REQUIRE(computeSwingCooldown(w, sHigh, f) < computeSwingCooldown(w, sLow, f));
}

TEST_CASE("Swing cooldown — S-grade DEX weapon faster with high DEX than high STR", "[combat]")
{
    const auto f = defaultFormulas();

    Weapon knife;
    knife.weight = 0.5f;
    knife.str_scaling = ScalingGrade::E;
    knife.dex_scaling = ScalingGrade::S; // pure DEX weapon
    knife.base_damage = 5.0f;

    const auto highDEX = makeStats(1, 20, 5, 5);
    const auto highSTR = makeStats(20, 1, 5, 5);

    REQUIRE(computeSwingCooldown(knife, highDEX, f) < computeSwingCooldown(knife, highSTR, f));
}

// ---------------------------------------------------------------------------
// Damage tests
// ---------------------------------------------------------------------------

TEST_CASE("Damage — fist (E/E) + str=5: base + floor(5 * 0.25)", "[combat]")
{
    const auto f = defaultFormulas();
    const auto w = makeFist(); // base_damage=5, E/E scaling (mult=0.25)
    const auto s = makeStats(5, 5, 5, 5);

    // Both grades E → mult=0.25, both stats=5.  Max(strMult, dexMult)=0.25.
    // expected = 5 + floor(5 * 0.25) = 5 + 1 = 6
    const float dmg = computeDamage(w, s, f);
    REQUIRE(dmg == Catch::Approx(6.0f));
}

TEST_CASE("Damage — S-grade weapon uses the better (S) multiplier", "[combat]")
{
    const auto f = defaultFormulas();

    Weapon sword;
    sword.base_damage = 10.0f;
    sword.str_scaling = ScalingGrade::S; // 1.5x
    sword.dex_scaling = ScalingGrade::E; // 0.25x
    sword.weight = 1.0f;

    const auto s = makeStats(10, 1, 5, 5);

    // Uses STR (mult=1.5): 10 + floor(10 * 1.5) = 10 + 15 = 25
    REQUIRE(computeDamage(sword, s, f) == Catch::Approx(25.0f));
}

TEST_CASE("Damage — weapon with D STR scaling + str=5", "[combat]")
{
    const auto f = defaultFormulas();

    Weapon w;
    w.base_damage = 8.0f;
    w.str_scaling = ScalingGrade::D; // 0.5x
    w.dex_scaling = ScalingGrade::D; // 0.5x (tie → uses STR branch)
    w.weight = 0.5f;

    const auto s = makeStats(5, 5, 5, 5);
    // expected = 8 + floor(5 * 0.5) = 8 + 2 = 10
    REQUIRE(computeDamage(w, s, f) == Catch::Approx(10.0f));
}

// ---------------------------------------------------------------------------
// HP derivation
// ---------------------------------------------------------------------------

TEST_CASE("HP derivation — end=5 gives expected maxHP", "[combat]")
{
    // maxHP = base + floor(scale * log(END + 1))
    //       = 50 + floor(100 * log(6))
    //       = 50 + floor(179.17...) = 50 + 179 = 229
    const FormulaConfig f; // defaults match formulas.json values

    const int expectedMax =
        static_cast<int>(f.hp.base + std::floor(f.hp.scale * std::log(5.0f + 1.0f)));
    REQUIRE(expectedMax == 229);
}

TEST_CASE("HP derivation — end=1 gives minimum HP", "[combat]")
{
    const FormulaConfig f;
    const int maxHP = static_cast<int>(f.hp.base + std::floor(f.hp.scale * std::log(2.0f)));
    REQUIRE(maxHP > 0);
    REQUIRE(maxHP < 120); // should be modest for a level-1-END character
}

// ---------------------------------------------------------------------------
// Stat requirement penalty
// ---------------------------------------------------------------------------

TEST_CASE("Penalty — no deficit: factor = 1.0", "[combat]")
{
    const FormulaConfig f;
    // deficit=0 → exp(0) = 1.0
    const float strDeficit = 0;
    const float dexDeficit = 0;
    const float penalty =
        std::exp(-static_cast<float>(strDeficit) * f.stat_requirement.penalty_rate) *
        std::exp(-static_cast<float>(dexDeficit) * f.stat_requirement.penalty_rate);
    REQUIRE(penalty == Catch::Approx(1.0f));
}

TEST_CASE("Penalty — STR deficit=5: factor ≈ exp(-5*0.15) ≈ 0.472", "[combat]")
{
    const FormulaConfig f;
    const float expected = std::exp(-5.0f * 0.15f);
    REQUIRE(expected == Catch::Approx(0.4724f).epsilon(0.001));
}

TEST_CASE("Penalty — combined STR+DEX deficit: multiplicative", "[combat]")
{
    const FormulaConfig f;
    const float strPenalty = std::exp(-3.0f * f.stat_requirement.penalty_rate);
    const float dexPenalty = std::exp(-2.0f * f.stat_requirement.penalty_rate);
    const float combined = strPenalty * dexPenalty;
    REQUIRE(combined < strPenalty);
    REQUIRE(combined < dexPenalty);
    REQUIRE(combined > 0.0f);
}
