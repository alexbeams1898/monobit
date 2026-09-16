#include "Formulas.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("glowBrightness soft-caps toward max as Perception grows", "[formulas]")
{
    formulas::Config const cfg; // defaults: max 0.55, half_at 6

    // Perception 0 -> 0 (a quiet world, no glow).
    REQUIRE(formulas::glowBrightness(cfg, 0) == Approx(0.0f));
    // Faint at the low end (search-for-answers): P1 = max * 1/7.
    REQUIRE(formulas::glowBrightness(cfg, 1) == Approx(0.55f * 1.0f / 7.0f));
    // Exactly half of max at Perception == half_at.
    REQUIRE(formulas::glowBrightness(cfg, 6) == Approx(0.55f * 0.5f));
    // Approaches but never reaches max, however high Perception climbs.
    REQUIRE(formulas::glowBrightness(cfg, 1000) < cfg.glow.max);
    REQUIRE(formulas::glowBrightness(cfg, 1000) == Approx(cfg.glow.max).margin(0.01f));
    // Monotonic: more Perception is always at least as bright.
    REQUIRE(formulas::glowBrightness(cfg, 3) > formulas::glowBrightness(cfg, 1));
    REQUIRE(formulas::glowBrightness(cfg, 10) > formulas::glowBrightness(cfg, 6));
}

TEST_CASE("glowBrightness half_at tunes how fast the ramp climbs", "[formulas]")
{
    formulas::Config slow;
    slow.glow.max = 0.55f;
    slow.glow.half_at = 20.0f; // very gradual -- faint for a long time
    formulas::Config fast;
    fast.glow.max = 0.55f;
    fast.glow.half_at = 2.0f; // brightens quickly

    // At the same Perception, the smaller half_at is brighter.
    REQUIRE(formulas::glowBrightness(fast, 3) > formulas::glowBrightness(slow, 3));

    // half_at <= 0 degenerates to always-full (guard against divide-by-zero).
    formulas::Config full;
    full.glow.half_at = 0.0f;
    REQUIRE(formulas::glowBrightness(full, 0) == Approx(full.glow.max));
}
