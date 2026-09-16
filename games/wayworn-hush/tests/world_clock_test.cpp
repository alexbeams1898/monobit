#include "WorldClock.h"

#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

using worldclock::WorldClock;

TEST_CASE("A fresh clock is on day 1", "[worldclock]")
{
    WorldClock const c;
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

TEST_CASE("the default cadence keeps scene-time unhurried", "[worldclock]")
{
    // seconds_per_day / 1440 is how long an in-world minute takes on the ONE
    // realtime path (scenes). Pins the FEEL: a short scene should cost a few
    // minutes of the morning, not an hour -- outside scenes the hour only moves
    // by participation costs, so this dial touches nothing else.
    const WorldClock c;
    const double realSecondsPerMinute = c.seconds_per_day / 1440.0;
    REQUIRE(realSecondsPerMinute >= 8.0);
    REQUIRE(realSecondsPerMinute <= 20.0);
}

TEST_CASE("clock times parse to day fractions; windows hold and wrap", "[worldclock]")
{
    REQUIRE(worldclock::parseClockTime("00:00") == Approx(0.0));
    REQUIRE(worldclock::parseClockTime("06:00") == Approx(0.25));
    REQUIRE(worldclock::parseClockTime("18:30") == Approx(18.5 / 24.0));
    REQUIRE(worldclock::parseClockTime("24:00") == Approx(1.0)); // end-of-day is authorable
    REQUIRE(worldclock::parseClockTime("25:00") < 0.0);
    REQUIRE(worldclock::parseClockTime("junk") < 0.0);
    REQUIRE(worldclock::parseClockTime("7") < 0.0);

    REQUIRE(worldclock::inWindow(0.5, 0.25, 0.75));
    REQUIRE_FALSE(worldclock::inWindow(0.8, 0.25, 0.75));
    // A night shift wraps midnight: 22:00-06:00 holds at 23:00 and 05:00, not at noon.
    const double from = 22.0 / 24.0;
    const double to = 6.0 / 24.0;
    REQUIRE(worldclock::inWindow(23.0 / 24.0, from, to));
    REQUIRE(worldclock::inWindow(5.0 / 24.0, from, to));
    REQUIRE_FALSE(worldclock::inWindow(0.5, from, to));
}

TEST_CASE("fractionOfDay walks the day and wraps into the next", "[worldclock]")
{
    worldclock::WorldClock c;
    c.seconds_per_day = 100.0;
    c.seconds = 25.0;
    REQUIRE(worldclock::fractionOfDay(c) == Approx(0.25));
    c.seconds = 250.0; // two and a half days in -> quarter past the third day's start
    REQUIRE(worldclock::fractionOfDay(c) == Approx(0.5));
}

TEST_CASE("the authored start_time seeds where a fresh walk's clock begins", "[worldclock]")
{
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_clock_start.json").string();
    std::ofstream(path) << R"({ "seconds_per_day": 4320.0, "start_time": "10:47" })";
    worldclock::WorldClock c;
    worldclock::load(c, path);
    REQUIRE(c.start_seconds == Approx((10.0 + 47.0 / 60.0) / 24.0 * 4320.0));
    // The reading a walk seeded from it would show.
    REQUIRE(worldclock::timeAt(c, c.start_seconds) == "10:47");

    // No start_time authored -> a walk starts at the day's beginning, as before.
    std::ofstream(path) << R"({ "seconds_per_day": 4320.0 })";
    worldclock::WorldClock plain;
    worldclock::load(plain, path);
    REQUIRE(plain.start_seconds == 0.0);
}

TEST_CASE("advanceMinutes moves the face by in-world minutes, cadence-independent", "[worldclock]")
{
    WorldClock fast;
    fast.seconds_per_day = 240.0;
    WorldClock slow;
    slow.seconds_per_day = 4320.0;
    worldclock::advanceMinutes(fast, 90.0);
    worldclock::advanceMinutes(slow, 90.0);
    REQUIRE(worldclock::timeAt(fast, fast.seconds) == "1:30");
    REQUIRE(worldclock::timeAt(slow, slow.seconds) == "1:30");
    worldclock::advanceMinutes(fast, -5.0); // a negative cost is a no-op, never rewinds
    REQUIRE(worldclock::timeAt(fast, fast.seconds) == "1:30");
}

TEST_CASE("participation costs load from config", "[worldclock]")
{
    const auto path =
        (std::filesystem::temp_directory_path() / "wayworn_clock_costs.json").string();
    std::ofstream(path) << R"({ "seconds_per_day": 4320.0,
        "costs": { "walk_minutes_per_100px": 1.5, "deed_minutes": 7.0 } })";
    WorldClock c;
    worldclock::load(c, path);
    REQUIRE(c.costs.walk_minutes_per_100px == Approx(1.5));
    REQUIRE(c.costs.deed_minutes == Approx(7.0));
    REQUIRE(c.costs.craft_minutes == Approx(10.0)); // unauthored keeps its default
}
