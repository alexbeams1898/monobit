#include "SpiritHud.h"

#include <catch2/catch_test_macros.hpp>

// The counting half of the Spirit readout (tick/gained/reset). Drawing needs a font and a GL
// context and is integration-tested by playing; the COUNTING is what broke, and it is pure.
//
// The bug these pin: the engine credits Spirit in whole batches the instant it runs, ahead of
// the lines that explain it. A counter climbing toward the true total therefore jumps to the
// sum of a chain while the "+N" labels are still trickling out behind it. The counter must
// climb toward what has been ANNOUNCED, and only reconcile to the truth once nothing is left
// to read.

namespace
{
spirit_hud::Config fastConfig()
{
    spirit_hud::Config c;
    c.catch_up = 1.0f; // close the whole gap each tick, so a test needs one tick, not twenty
    c.min_step = 1;
    return c;
}

// Run enough ticks for the climb to settle at its target.
void settleClimb(const spirit_hud::Config& c, int truth, bool settled)
{
    for (int i = 0; i < 8; ++i)
        spirit_hud::tick(c, truth, settled, 0.016f);
}
} // namespace

TEST_CASE("a chain of gains feeds in one at a time, not as their sum", "[spirit_hud]")
{
    const spirit_hud::Config c = fastConfig();
    spirit_hud::reset(0);

    // The engine banks BOTH rewards at once -- the world already knows he earned 10.
    const int truth = 10;

    // ...but only the first line has surfaced. Nothing is settled: a box is up, more is queued.
    spirit_hud::gained(5);
    settleClimb(c, truth, /*settled=*/false);
    REQUIRE(spirit_hud::shown() == 5); // NOT 10 -- the second 5 has not been explained yet

    // The second line surfaces and says its own +5.
    spirit_hud::gained(5);
    settleClimb(c, truth, /*settled=*/false);
    REQUIRE(spirit_hud::shown() == 10);
}

TEST_CASE("the counter never runs ahead of what has been announced", "[spirit_hud]")
{
    // The heart of it: a big batch banked, nothing said about it yet.
    const spirit_hud::Config c = fastConfig();
    spirit_hud::reset(0);

    settleClimb(c, /*truth=*/40, /*settled=*/false);
    REQUIRE(spirit_hud::shown() == 0); // he has 40; he has been told about none of it
}

TEST_CASE("once nothing is left to read, the counter reconciles to the truth", "[spirit_hud]")
{
    // Rewards can be credited with no line to explain them (a deed, a flag's cascade). The
    // settled pass is what keeps those from being lost to the counter forever.
    const spirit_hud::Config c = fastConfig();
    spirit_hud::reset(0);

    settleClimb(c, /*truth=*/12, /*settled=*/false);
    REQUIRE(spirit_hud::shown() == 0);

    settleClimb(c, /*truth=*/12, /*settled=*/true);
    REQUIRE(spirit_hud::shown() == 12);
}

TEST_CASE("spending snaps down rather than climbing", "[spirit_hud]")
{
    const spirit_hud::Config c = fastConfig();
    spirit_hud::reset(50);
    REQUIRE(spirit_hud::shown() == 50);

    // He spent 30 at a lean-to. A drop is not a climb -- it is simply so.
    spirit_hud::tick(c, /*spirit=*/20, /*settled=*/true, 0.016f);
    REQUIRE(spirit_hud::shown() == 20);
}

TEST_CASE("a walk opens at its real total instead of climbing from the last one", "[spirit_hud]")
{
    // The load-time rule, applied here: an arrival is not a gain. Without reset the counter
    // would climb from whatever the previous walk left on screen.
    const spirit_hud::Config c = fastConfig();
    spirit_hud::reset(90);
    spirit_hud::tick(c, 90, /*settled=*/true, 0.016f);
    REQUIRE(spirit_hud::shown() == 90);

    spirit_hud::reset(3); // a different pilgrim, three Spirit to his name
    REQUIRE(spirit_hud::shown() == 3);
}

TEST_CASE("the climb eases in rather than jumping", "[spirit_hud]")
{
    // The feel the souls counter is for: a big understanding reads as a long climb. With a
    // realistic catch_up the number must NOT arrive in one frame.
    spirit_hud::Config c;
    c.catch_up = 0.08f;
    c.min_step = 1;
    spirit_hud::reset(0);

    spirit_hud::gained(100);
    spirit_hud::tick(c, 100, /*settled=*/false, 0.016f);
    const int afterOne = spirit_hud::shown();
    REQUIRE(afterOne > 0);   // it moved
    REQUIRE(afterOne < 100); // but it did not arrive

    settleClimb(c, 100, /*settled=*/false);
    for (int i = 0; i < 200; ++i)
        spirit_hud::tick(c, 100, /*settled=*/false, 0.016f);
    REQUIRE(spirit_hud::shown() == 100); // and it does get there
}
