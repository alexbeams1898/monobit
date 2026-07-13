#include "WorldClock.h"

#include <catch2/catch_test_macros.hpp>

using worldclock::WorldClock;

TEST_CASE("A fresh clock is on day 1", "[worldclock]")
{
    WorldClock c;
    REQUIRE(worldclock::day(c) == 1);
    REQUIRE(worldclock::stamp(c) == "Day 1");
}

TEST_CASE("Ticking accumulates world time and rolls the day at seconds_per_day", "[worldclock]")
{
    WorldClock c;
    c.seconds_per_day = 100.0;

    worldclock::tick(c, 50.0);
    REQUIRE(worldclock::day(c) == 1); // half a day in

    worldclock::tick(c, 50.0); // exactly one day
    REQUIRE(worldclock::day(c) == 2);
    REQUIRE(worldclock::stamp(c) == "Day 2");

    worldclock::tick(c, 250.0); // +2.5 days -> day 4
    REQUIRE(worldclock::day(c) == 4);
}

TEST_CASE("A zero/invalid seconds_per_day never divides by zero", "[worldclock]")
{
    WorldClock c;
    c.seconds_per_day = 0.0;
    worldclock::tick(c, 999.0);
    REQUIRE(worldclock::day(c) == 1); // degrades to day 1, no crash
}
