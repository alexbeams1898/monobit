// Tests for the universal-axis weapon damage formula on
// computeAttackDamage(Stats, DamageInputs, identity_value,
// identity_scaling). Verifies:
//   - base + STR*str_scaling + DEX*dex_scaling + END*end_scaling +
//     LCK*lck_scaling (Mind axes hardwired to 0 in v1 -- documented in
//     Actor.cpp; not exercised here so we don't lock the formula
//     against future Stats expansion).
//   - Identity contribution adds floor(identity_value * identity_scaling).
//   - Negative or zero subtotals floor to 0 (no negative damage).
//   - Floor semantics match per-axis (each axis floors independently;
//     fractional contributions don't accumulate across axes).

#include "gameplay/Actor.h"

#include <catch2/catch_test_macros.hpp>

using selva::gameplay::computeAttackDamage;
using selva::gameplay::DamageInputs;
using selva::gameplay::Stats;

namespace
{

Stats statsOf(int str, int dex, int end, int lck)
{
    Stats s;
    s.str = str;
    s.dex = dex;
    s.end = end;
    s.lck = lck;
    return s;
}

} // namespace

TEST_CASE("computeAttackDamage: base-only weapon with no stats", "[combat][damage]")
{
    DamageInputs di;
    di.base_damage = 10.0f;
    // All scalings remain 0.
    const Stats s = statsOf(99, 99, 99, 99);
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 10);
}

TEST_CASE("computeAttackDamage: STR-scaled weapon scales with STR", "[combat][damage]")
{
    DamageInputs di;
    di.base_damage = 12.0f;
    di.str_scaling = 1.0f;
    const Stats s = statsOf(15, 0, 0, 0);
    // base 12 + floor(15 * 1.0) = 27
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 27);
}

TEST_CASE("computeAttackDamage: DEX-scaled weapon scales with DEX", "[combat][damage]")
{
    DamageInputs di;
    di.base_damage = 8.0f;
    di.dex_scaling = 0.5f;
    const Stats s = statsOf(0, 20, 0, 0);
    // base 8 + floor(20 * 0.5) = 18
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 18);
}

TEST_CASE("computeAttackDamage: END-scaled (lead mace shape)", "[combat][damage]")
{
    // Mirrors lead_mace.json: base 12, STR 0.6, END 1.0
    DamageInputs di;
    di.base_damage = 12.0f;
    di.str_scaling = 0.6f;
    di.end_scaling = 1.0f;
    const Stats s = statsOf(10, 0, 20, 0);
    // base 12 + floor(10 * 0.6=6) + floor(20 * 1.0=20) = 38
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 38);
}

TEST_CASE("computeAttackDamage: LCK-scaled (bone knife shape)", "[combat][damage]")
{
    // Mirrors bone_knife.json's DEX/LCK/INT shape (INT collapses to 0
    // in v1 -- documented in computeAttackDamage). Just DEX + LCK.
    DamageInputs di;
    di.base_damage = 10.0f;
    di.dex_scaling = 1.0f;
    di.lck_scaling = 0.3f;
    const Stats s = statsOf(0, 15, 0, 10);
    // base 10 + floor(15 * 1.0=15) + floor(10 * 0.3=3) = 28
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 28);
}

TEST_CASE("computeAttackDamage: identity contribution adds on-class", "[combat][damage]")
{
    // Lead-mace-shape inputs; simulate a Penitent wielder with
    // identity_value = 5 (e.g. Vitality at L5) and the locked 0.75
    // identity_stat_scaling.
    DamageInputs di;
    di.base_damage = 12.0f;
    di.str_scaling = 0.6f;
    di.end_scaling = 1.0f;
    const Stats s = statsOf(10, 0, 20, 0);
    // Without identity: 38 (from prior test). With identity 5 * 0.75
    // = 3.75 floored to 3 -> total 41.
    REQUIRE(computeAttackDamage(s, di, 5, 0.75f) == 41);
}

TEST_CASE("computeAttackDamage: identity contribution zero scaling adds nothing",
          "[combat][damage]")
{
    // Off-class wielder: resolver sets identity_scaling = 0.0f, so even
    // if identity_value were non-zero it should not contribute.
    DamageInputs di;
    di.base_damage = 10.0f;
    di.dex_scaling = 1.0f;
    const Stats s = statsOf(0, 10, 0, 0);
    REQUIRE(computeAttackDamage(s, di, 999, 0.0f) == 20);
}

TEST_CASE("computeAttackDamage: zero base + zero stats yields zero", "[combat][damage]")
{
    DamageInputs di;
    const Stats s = statsOf(0, 0, 0, 0);
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 0);
}

TEST_CASE("computeAttackDamage: negative subtotal floors to 0", "[combat][damage]")
{
    // The formula sums positives only in practice (no axis is
    // negative), but the negative-base guard is doctrinal: a downstream
    // tuning bug shouldn't deal negative damage. Verify the floor.
    DamageInputs di;
    di.base_damage = -5.0f;
    const Stats s = statsOf(0, 0, 0, 0);
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 0);
}

TEST_CASE("computeAttackDamage: per-axis floor semantics (no fractional carry)", "[combat][damage]")
{
    // Two axes each contribute 0.5 * stat = 0.5 floored to 0
    // individually. Sum is 0 + 0 = 0, NOT 1 (which a single-flooring
    // formula would yield). This locks per-axis floor semantics so
    // future tuning can't accidentally make tiny scalings stack.
    DamageInputs di;
    di.str_scaling = 0.5f;
    di.dex_scaling = 0.5f;
    const Stats s = statsOf(1, 1, 0, 0);
    // floor(1 * 0.5) + floor(1 * 0.5) = 0 + 0 = 0
    REQUIRE(computeAttackDamage(s, di, 0, 0.0f) == 0);
}

TEST_CASE("computeAttackDamage: fist 4-param overload unchanged (regression)", "[combat][damage]")
{
    // The legacy 4-param overload is the unarmed-fallback path. Verify
    // it still yields the same shape so the fist-default code path
    // doesn't drift when the multi-axis form evolves.
    const Stats s = statsOf(10, 10, 0, 0);
    // base 5 + floor(10 * 0.5=5) + floor(10 * 0.5=5) = 15
    REQUIRE(computeAttackDamage(s, 5.0f, 0.5f, 0.5f) == 15);
}
