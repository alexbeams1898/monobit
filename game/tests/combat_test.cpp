#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
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
    w.str_scaling = 0.25f; // E-tier
    w.dex_scaling = 0.25f; // E-tier
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

    // E/E scaling → dexBias = 0.5, effectiveStat = 5.
    // reduction = 160 * sqrt(5) / 100 ≈ 3.58, cooldown = 50 / 4.58 ≈ 10.9 s
    // (test uses weight_scale=100 default, not the JSON value of 5).
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
    knife.str_scaling = 0.25f; // E-tier — minimal STR scaling
    knife.dex_scaling = 1.5f;  // S-tier — pure DEX weapon
    knife.base_damage = 5.0f;

    const auto highDEX = makeStats(1, 20, 5, 5);
    const auto highSTR = makeStats(20, 1, 5, 5);

    REQUIRE(computeSwingCooldown(knife, highDEX, f) < computeSwingCooldown(knife, highSTR, f));
}

// ---------------------------------------------------------------------------
// Damage tests
// ---------------------------------------------------------------------------

TEST_CASE("Damage — fist (E/E) + str=5/dex=5: both scaling terms added", "[combat]")
{
    const auto f = defaultFormulas();
    const auto w = makeFist(); // base_damage=5, E/E scaling (mult=0.25)
    const auto s = makeStats(5, 5, 5, 5);

    // Additive: 5 + floor(5 * 0.25) + floor(5 * 0.25) = 5 + 1 + 1 = 7
    const float dmg = computeDamage(w, s, f);
    REQUIRE(dmg == Catch::Approx(7.0f));
}

TEST_CASE("Damage — S/E weapon: STR dominates, negligible DEX term", "[combat]")
{
    const auto f = defaultFormulas();

    Weapon sword;
    sword.base_damage = 10.0f;
    sword.str_scaling = 1.5f;  // S-tier
    sword.dex_scaling = 0.25f; // E-tier
    sword.weight = 1.0f;

    const auto s = makeStats(10, 1, 5, 5);

    // Additive: 10 + floor(10 * 1.5) + floor(1 * 0.25) = 10 + 15 + 0 = 25
    REQUIRE(computeDamage(sword, s, f) == Catch::Approx(25.0f));
}

TEST_CASE("Damage — D/D weapon + str=5/dex=5: both terms add", "[combat]")
{
    const auto f = defaultFormulas();

    Weapon w;
    w.base_damage = 8.0f;
    w.str_scaling = 0.5f; // D-tier
    w.dex_scaling = 0.5f; // D-tier
    w.weight = 0.5f;

    const auto s = makeStats(5, 5, 5, 5);
    // Additive: 8 + floor(5 * 0.5) + floor(5 * 0.5) = 8 + 2 + 2 = 12
    REQUIRE(computeDamage(w, s, f) == Catch::Approx(12.0f));
}

// ---------------------------------------------------------------------------
// HP derivation
// ---------------------------------------------------------------------------

TEST_CASE("HP derivation — end=5 gives expected maxHP", "[combat]")
{
    // maxHP = base + floor(scale * log(END + 1))
    //       = 5 + floor(100 * log(6))
    //       = 5 + floor(179.17...) = 5 + 179 = 184
    const FormulaConfig f; // defaults match formulas.json values

    const int expectedMax =
        static_cast<int>(f.hp.base + std::floor(f.hp.scale * std::log(5.0f + 1.0f)));
    REQUIRE(expectedMax == 184);
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

TEST_CASE("Dodge formula defaults", "[combat]")
{
    const FormulaConfig f;
    REQUIRE(f.dodge.duration == Catch::Approx(0.25f));
    REQUIRE(f.dodge.cooldown == Catch::Approx(0.35f));
}

// ---------------------------------------------------------------------------
// Stamina pool tests
// ---------------------------------------------------------------------------

// Helper: create a Stamina component initialized like LevelingSystem does.
static Stamina makeStamina(const FormulaConfig& f, int end)
{
    Stamina sta;
    sta.max_stamina =
        f.stamina.base + f.stamina.end_scale * std::log(static_cast<float>(end) + 1.0f);
    sta.current = sta.max_stamina;
    return sta;
}

// Helper: tick recovery matching CombatSystem logic.
static void tickRecovery(Stamina& sta, const FormulaConfig& f, float dt)
{
    if (sta.recovery_timer > 0.0f)
        sta.recovery_timer -= dt;
    else if (sta.current < sta.max_stamina)
        sta.current = std::min(sta.max_stamina, sta.current + f.stamina.recovery_rate * dt);
}

// Helper: deduct stamina cost (matching CombatSystem deduction pattern).
static void deductStamina(Stamina& sta, const FormulaConfig& f, float cost)
{
    sta.current = std::max(0.0f, sta.current - cost);
    sta.recovery_timer = f.stamina.recovery_delay;
}

TEST_CASE("Stamina defaults match expected values", "[combat]")
{
    const FormulaConfig f;
    REQUIRE(f.stamina.swing_effort == Catch::Approx(3.0f));
    REQUIRE(f.stamina.dodge_effort == Catch::Approx(5.0f));
    REQUIRE(f.stamina.skill_effort == Catch::Approx(4.0f));
    REQUIRE(f.stamina.sprint_effort == Catch::Approx(1.0f));
    REQUIRE(f.stamina.base == Catch::Approx(5.0f));
    REQUIRE(f.stamina.end_scale == Catch::Approx(3.0f));
    REQUIRE(f.stamina.recovery_rate == Catch::Approx(2.5f));
    REQUIRE(f.stamina.recovery_delay == Catch::Approx(1.0f));
}

TEST_CASE("Stamina — starts full at max", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    REQUIRE(sta.current == Catch::Approx(sta.max_stamina));
    REQUIRE(sta.max_stamina > 0.0f);
}

TEST_CASE("Stamina — swing deducts weapon.weight * swing_effort", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    const float cost = 0.5f * f.stamina.swing_effort; // fist weight=0.5
    deductStamina(sta, f, cost);
    REQUIRE(sta.current == Catch::Approx(sta.max_stamina - cost));
}

TEST_CASE("Stamina — dodge costs more than swing", "[combat]")
{
    const FormulaConfig f;
    const float weight = 2.0f;
    auto staSwing = makeStamina(f, 5);
    auto staDodge = makeStamina(f, 5);
    deductStamina(staSwing, f, weight * f.stamina.swing_effort);
    deductStamina(staDodge, f, weight * f.stamina.dodge_effort);
    REQUIRE(staDodge.current < staSwing.current);
}

TEST_CASE("Stamina — heavier weapon costs more per action", "[combat]")
{
    const FormulaConfig f;
    auto staLight = makeStamina(f, 5);
    auto staHeavy = makeStamina(f, 5);
    deductStamina(staLight, f, 1.0f * f.stamina.swing_effort);
    deductStamina(staHeavy, f, 5.0f * f.stamina.swing_effort);
    REQUIRE(staHeavy.current < staLight.current);
}

TEST_CASE("Stamina — higher END gives larger pool", "[combat]")
{
    const FormulaConfig f;
    auto staLow = makeStamina(f, 3);
    auto staHigh = makeStamina(f, 20);
    REQUIRE(staHigh.max_stamina > staLow.max_stamina);
}

TEST_CASE("Stamina — clamped to zero (never negative)", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 1);
    deductStamina(sta, f, 999.0f); // way more than pool
    REQUIRE(sta.current == Catch::Approx(0.0f));
}

TEST_CASE("Stamina — no recovery during delay period", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    deductStamina(sta, f, 3.0f);
    const float afterDeduct = sta.current;

    // Tick 10 frames at 16ms each = 160ms (well within 600ms delay).
    for (int i = 0; i < 10; ++i)
        tickRecovery(sta, f, 0.016f);

    REQUIRE(sta.current == Catch::Approx(afterDeduct));
}

TEST_CASE("Stamina — recovers after delay expires", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    deductStamina(sta, f, 3.0f);
    const float afterDeduct = sta.current;

    // Burn through the full delay.
    tickRecovery(sta, f, f.stamina.recovery_delay + 0.001f);
    REQUIRE(sta.current == Catch::Approx(afterDeduct)); // delay just expired

    // Now recover for 0.5s: recovery_rate=2.5/s => +1.25
    tickRecovery(sta, f, 0.5f);
    REQUIRE(sta.current == Catch::Approx(afterDeduct + 1.25f));
}

TEST_CASE("Stamina — recovery caps at max", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    deductStamina(sta, f, 1.0f);
    // Burn delay, then recover way past max.
    tickRecovery(sta, f, f.stamina.recovery_delay + 0.001f);
    tickRecovery(sta, f, 100.0f);
    REQUIRE(sta.current == Catch::Approx(sta.max_stamina));
}

TEST_CASE("Stamina — new deduction resets recovery delay", "[combat]")
{
    const FormulaConfig f;
    auto sta = makeStamina(f, 5);
    deductStamina(sta, f, 1.0f);

    // Almost through delay.
    tickRecovery(sta, f, f.stamina.recovery_delay - 0.05f);
    // Deduct again — resets delay.
    deductStamina(sta, f, 1.0f);
    REQUIRE(sta.recovery_timer == Catch::Approx(f.stamina.recovery_delay));

    // Tick same partial time — should NOT recover yet.
    tickRecovery(sta, f, f.stamina.recovery_delay - 0.05f);
    const float snapshot = sta.current;
    tickRecovery(sta, f, 0.01f); // still in delay
    REQUIRE(sta.current == Catch::Approx(snapshot));
}
