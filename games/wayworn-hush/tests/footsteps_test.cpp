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
    c.pools["Grass"] = {"a.ogg"}; // pool content is irrelevant to the pure cadence logic
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

TEST_CASE("A surface picks its own pool; untagged falls back to default", "[footsteps]")
{
    Config c;
    c.default_surface = "Grass";
    c.pools["Grass"] = {"grass.ogg"};
    c.pools["Sand"] = {"sand.ogg"};
    // A named surface resolves to its own pool.
    REQUIRE(footsteps::poolFor(c, "Sand") == &c.pools["Sand"]);
    REQUIRE(footsteps::poolFor(c, "Grass") == &c.pools["Grass"]);
    // An untagged tile (empty surface) uses the default pool.
    REQUIRE(footsteps::poolFor(c, "") == &c.pools["Grass"]);
}

TEST_CASE("A tagged surface with no pool is silent (does NOT borrow the default)", "[footsteps]")
{
    Config c;
    c.default_surface = "Grass";
    c.pools["Grass"] = {"grass.ogg"};
    // Water is a real tag with no pool -> silent by intent, not a grass step. This is
    // the load-bearing rule for "can't hear footsteps while swimming".
    REQUIRE(footsteps::poolFor(c, "Water") == nullptr);
    // An unknown/misspelled surface is likewise silent rather than falling back.
    REQUIRE(footsteps::poolFor(c, "Nonexistent") == nullptr);
}

TEST_CASE("The authored footsteps config loads pools per surface", "[footsteps]")
{
    // Real config/surfaces alignment: the shipped config has Grass/Sand/Bridge pools
    // and no Water pool. Test working dir is the game source root (CMake WORKING_DIR).
    Config c;
    footsteps::load(c, "config/footsteps.json");
    REQUIRE(footsteps::poolFor(c, "Grass") != nullptr);
    REQUIRE(footsteps::poolFor(c, "Sand") != nullptr);
    REQUIRE(footsteps::poolFor(c, "Bridge") != nullptr);
    REQUIRE(footsteps::poolFor(c, "Water") == nullptr); // silent until swim exists
}
