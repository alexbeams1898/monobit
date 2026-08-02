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
    // The day<->24-hour-face mapping, AND how fast the hour flows during scenes
    // (the one realtime path -- see Costs). Config is the dial.
    double seconds_per_day = 17280.0;
    // Where a FRESH walk's clock starts (elapsed seconds into day 1) -- authored
    // as "start_time" ("HH:MM") in config. The opening is a specific moment of a
    // specific morning, not midnight; a resumed walk keeps its own time instead.
    double start_seconds = 0.0;

    // TIME IS PARTICIPATION: the hour advances from what the pilgrim DOES, never
    // from the wall clock -- standing still, the day waits (the world still
    // breathes in real time; only the hour holds). Costs are authored in
    // IN-WORLD MINUTES (cadence-independent); scenes are the one exception,
    // flowing at the ambient cadence while they hold the floor (the world acting
    // takes the time it takes).
    // Defaults are for the SMALL case -- a look, a word, a door. Anything that is
    // real work says so itself (a deed's authored `minutes`), so the default never
    // has to be big enough for clearing a trail.
    struct Costs
    {
        double walk_minutes_per_100px = 0.15; // the metronome: crossing ground
        double observe_minutes = 0.5;         // a deliberate reading -- a look
        double deed_minutes = 0.5;            // a deed that names no time (a word)
        double craft_minutes = 10.0;          // a making
        double warp_minutes = 0.5;            // a threshold crossed
    } costs;
};

// Load the cadence from config/world_clock.json (silent no-op -> defaults if missing).
// Only `seconds_per_day` is authored -- `seconds` is a walk's elapsed time, which belongs
// to the save, not to config.
void load(WorldClock& clock, const std::string& path);

// Advance the clock by dt seconds of world time -- the SCENE path only (the one
// place the hour flows with the wall clock; see Costs).
void tick(WorldClock& clock, double dt);

// Advance by an authored cost, in in-world minutes -- the participation path
// (walking, reading, deeds, crossings). Cadence-independent: a 3-minute deed is
// 3 minutes on the face however long a minute takes to pass.
void advanceMinutes(WorldClock& clock, double minutes);

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

// How far into the current day the clock is, as [0,1). The unit schedules speak
// in -- cadence-independent, so retuning seconds_per_day never moves anyone's
// appointments.
double fractionOfDay(const WorldClock& clock);

// An authored "HH:MM" (24-hour face) as a fraction of the day, or -1 on a
// malformed string. THE parser for schedule times -- one reading of "07:00"
// everywhere.
double parseClockTime(const std::string& hhmm);

// Is `frac` inside [from, to)? A window with from > to wraps midnight (a night
// shift: 22:00-06:00).
bool inWindow(double frac, double from, double to);

// A day-fraction spoken as a clock reading ("8:00") -- the same face timeAt shows, for a
// time that is a position in the day rather than a moment on the record (when a door opens).
std::string clockOfDay(double frac);

// The same fraction spoken the way someone without a watch would say it: "in the morning",
// "this afternoon", "this evening", "tonight". The unwatched half of every time the game
// tells him -- he always knows roughly where the sun is, never the hour.
std::string partOfDay(double frac);

} // namespace worldclock
