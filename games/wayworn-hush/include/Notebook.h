#pragma once

#include "Observations.h"
#include "WorldClock.h"

#include <string>
#include <unordered_map>
#include <vector>

// The notebook: the thoughts the pilgrim has reached. See docs/design/NOTEBOOK.md.
//
// It keeps only WHEN each landed. Which ones landed is already
// observations::State::fired, and the thoughts themselves are authored content
// reloaded each run; a copy here could disagree with either or go stale when a
// thought is re-tuned. Everything else is a read-time view.
namespace notebook
{

// When a thought landed: the world-clock moment, in seconds. The raw reading, not a
// day -- the day, the time of day, and the order within a day all derive from it, and
// a stored day could answer none of the other two. The world's time runs whether or
// not the pilgrim can read it, so this is always recorded; being ABLE to tell the time
// is a question for whoever displays a note.
//
// kUntimed is a moment that was never recorded at all -- a note carried over from a
// save written before the moment was kept. Not "it happened outside time".
inline constexpr double kUntimed = -1.0;

struct Record
{
    std::unordered_map<std::string, double> at; // thought id -> world seconds (or kUntimed)
};

// Keeps the FIRST note only -- re-noting would re-date a memory to whenever it was
// last looked at.
void note(Record& rec, const std::string& thought_id, double at_seconds);

struct Entry
{
    const observations::Thought* thought = nullptr; // into the authored table, never a copy
    double at = kUntimed;                           // world seconds, or kUntimed
};

// True if this note's moment is on record.
bool timed(const Entry& e);

// The thoughts he has reached -- only those. Chronological: the page is when he was
// there, and the moment is stored, so this is the true order he wrote them.
// Further orderings (sort/filter/search) belong here beside it -- the notebook owns
// what order its pages are in, not the screen that draws them.
std::vector<Entry> found(const Record& rec, const observations::State& obs);

// The same entries, split into the days he wrote them; the untimed gather last. The
// clock resolves a moment to a day, so both agree how long a day is.
struct Day
{
    int day = 0; // 0 = the untimed page
    std::vector<Entry> entries;
};
std::vector<Day> byDay(const Record& rec, const observations::State& obs,
                       const worldclock::WorldClock& clock);

} // namespace notebook
