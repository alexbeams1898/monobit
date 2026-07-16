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
    REQUIRE(worldclock::timeAt(c, 500.0).empty());
}

TEST_CASE("timeAt reads a moment onto a 24-hour face", "[worldclock]")
{
    WorldClock c;
    c.seconds_per_day = 240.0; // 4 minutes of play = a day -> 10s per in-world hour

    REQUIRE(worldclock::timeAt(c, 0.0) == "0:00");    // midnight, the day's start
    REQUIRE(worldclock::timeAt(c, 60.0) == "6:00");   // a quarter in
    REQUIRE(worldclock::timeAt(c, 120.0) == "12:00"); // midday
    REQUIRE(worldclock::timeAt(c, 125.0) == "12:30"); // minutes read too
}

TEST_CASE("timeAt is the time of DAY, not time since the walk began", "[worldclock]")
{
    // The face wraps every day: the same hour on day 3 reads the same as on day 1.
    WorldClock c;
    c.seconds_per_day = 240.0;
    REQUIRE(worldclock::timeAt(c, 60.0) == worldclock::timeAt(c, 60.0 + 240.0 * 2));
}

TEST_CASE("The cadence changes how long an hour takes, never what the clock says", "[worldclock]")
{
    // Retuning seconds_per_day must not shift the reading -- midday is midday however long
    // a day is made to last.
    WorldClock fast;
    fast.seconds_per_day = 100.0;
    WorldClock slow;
    slow.seconds_per_day = 1000.0;
    REQUIRE(worldclock::timeAt(fast, 50.0) == worldclock::timeAt(slow, 500.0));
}

TEST_CASE("stampAt carries the day and the time together", "[worldclock]")
{
    WorldClock c;
    c.seconds_per_day = 240.0;
    REQUIRE(worldclock::stampAt(c, 240.0 + 60.0) == "Day 2, 6:00");
}

TEST_CASE("stampAt on a clock that tells no hour says the day alone", "[worldclock]")
{
    // No cadence -> timeAt is empty. The stamp must not trail a comma into the gap.
    WorldClock c;
    c.seconds_per_day = 0.0;
    REQUIRE(worldclock::stampAt(c, 500.0) == "Day 1");
}

TEST_CASE("the default cadence is a minute every few seconds, not every second", "[worldclock]")
{
    // The dial the config authors: seconds_per_day / 1440 is how long an in-world minute
    // takes. Pins the FEEL -- a clock racing a minute per second reads as a stopwatch.
    const WorldClock c;
    const double realSecondsPerMinute = c.seconds_per_day / 1440.0;
    REQUIRE(realSecondsPerMinute >= 3.0);
    REQUIRE(realSecondsPerMinute <= 5.0);
}
