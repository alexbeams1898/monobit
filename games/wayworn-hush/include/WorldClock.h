#pragma once

#include <string>

// The in-game clock -- the single source for "when is it" in the world. Today it
// is a simple compressed-time day counter; the full day/night cycle + calendar
// (see docs/design/ENGINE.md) will grow the INTERNALS of this module without
// changing its interface, so callers (the notebook dateline, save data, later a
// day/night shader) read `stamp()` / the fields and never a hardcoded date.
//
// Diegetic note: the world always knows the time and always runs; a watch is an
// instrument that lets the PLAYER read it, never the thing that makes time pass. So
// this module hands out the reading and callers decide how much of it a pilgrim can
// tell. Time also feeds quests / time-gated drops / thoughts later.
namespace worldclock
{

// Accumulated world time. `seconds` advances only while the world runs (frozen
// while the pause page is open). `seconds_per_day` sets the compression -- how
// much real play-time is one in-world day; authored in config/world_clock.json.
struct WorldClock
{
    double seconds = 0.0;
    double seconds_per_day = 4320.0; // the default cadence; config is the dial
};

// Load the cadence from config/world_clock.json (silent no-op -> defaults if missing).
// Only `seconds_per_day` is authored -- `seconds` is a walk's elapsed time, which belongs
// to the save, not to config.
void load(WorldClock& clock, const std::string& path);

// Advance the clock by dt seconds of world time. Call once per fixed tick, only
// when the world is unfrozen.
void tick(WorldClock& clock, double dt);

// Whole in-world days elapsed (0-based -> day 1 is the first day).
int day(const WorldClock& clock);

// The same, for an elapsed span that isn't the live clock -- e.g. how far a saved
// pilgrim got. Takes the cadence from `clock` so both answers agree about how long a
// day is; a second copy of that division is how the two would drift apart.
int dayAt(const WorldClock& clock, double seconds);

// The display stamp for a notebook entry / HUD. Placeholder form "Day N" until the
// calendar arc supplies real dates + times; the notebook renders whatever this
// returns, so that arc is a change here, not in the box.
std::string stamp(const WorldClock& clock);

// The time of day at an elapsed moment ("6:20"). A day maps onto a 24-hour face
// however long `seconds_per_day` makes it, so retuning the cadence changes how long
// an hour takes to pass, never what the clock says.
std::string timeAt(const WorldClock& clock, double seconds);

// Day + time ("Day 2, 6:20") -- what a written entry is stamped with.
std::string stampAt(const WorldClock& clock, double seconds);

} // namespace worldclock
