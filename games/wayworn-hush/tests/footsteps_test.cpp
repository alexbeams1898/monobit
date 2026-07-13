#include "Footsteps.h"

#include <catch2/catch_test_macros.hpp>

using footsteps::Config;
using footsteps::State;

namespace
{
Config cfg()
{
    Config c;
    c.walk_cadence = 0.42f;
    c.run_cadence = 0.28f;
    c.grass = {"a.ogg"}; // pool content is irrelevant to the pure cadence logic
    return c;
}
} // namespace

TEST_CASE("A step fires immediately on the first moving frame", "[footsteps]")
{
    State s;
    const Config c = cfg();
    // Timer starts at 0, so the first moving tick lands a step at once.
    REQUIRE(footsteps::tick(s, c, /*moving=*/true, /*run=*/false, 0.016f));
}

TEST_CASE("After a step, no step fires until the cadence elapses", "[footsteps]")
{
    State s;
    const Config c = cfg();
    REQUIRE(footsteps::tick(s, c, true, false, 0.016f)); // first step -> timer = 0.42
    // Accumulate up to just under the walk cadence: no further step.
    float t = 0.0f;
    bool fired = false;
    while (t < 0.40f)
    {
        fired = footsteps::tick(s, c, true, false, 0.016f) || fired;
        t += 0.016f;
    }
    REQUIRE_FALSE(fired);
    // A few more ticks cross 0.42 -> the next step fires.
    bool next = false;
    for (int i = 0; i < 4; ++i)
        next = footsteps::tick(s, c, true, false, 0.016f) || next;
    REQUIRE(next);
}

TEST_CASE("Running fires steps faster than walking", "[footsteps]")
{
    const Config c = cfg();
    auto stepsIn = [&](bool run, float duration)
    {
        State s;
        int steps = 0;
        for (float t = 0.0f; t < duration; t += 0.016f)
            if (footsteps::tick(s, c, true, run, 0.016f))
                ++steps;
        return steps;
    };
    // Over the same window, the run cadence (0.28) yields more steps than walk (0.42).
    REQUIRE(stepsIn(/*run=*/true, 2.0f) > stepsIn(/*run=*/false, 2.0f));
}

TEST_CASE("Standing still fires no steps and resets the timer", "[footsteps]")
{
    State s;
    const Config c = cfg();
    footsteps::tick(s, c, true, false, 0.016f); // step -> timer = 0.42
    // Stop moving: no step, and the timer resets so the next move steps at once.
    REQUIRE_FALSE(footsteps::tick(s, c, /*moving=*/false, false, 0.016f));
    REQUIRE(s.step_timer == 0.0f);
    REQUIRE(footsteps::tick(s, c, /*moving=*/true, false, 0.016f)); // steps immediately
}
