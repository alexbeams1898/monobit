#include "Notebook.h"

#include <algorithm>

namespace notebook
{
namespace
{
// Moments ascending, with the untimed LAST -- "when unknown" reads after the times
// that are known, not before the first of them.
bool momentBefore(double a, double b)
{
    if (a == kUntimed)
        return false;
    if (b == kUntimed)
        return true;
    return a < b;
}
} // namespace

bool timed(const Entry& e)
{
    return e.at != kUntimed;
}

void note(Record& rec, const std::string& thought_id, double at_seconds)
{
    if (thought_id.empty())
        return;
    rec.at.emplace(thought_id, at_seconds); // emplace: the first note wins
}

std::vector<Entry> found(const Record& rec, const psyche::State& obs)
{
    std::vector<Entry> out;
    // The collection IS `fired` joined to the authored thoughts. Walking the thought
    // table (rather than `fired`) is what keeps the order stable: a hash set can't
    // reorder it, and a fired id with no thought behind it -- content removed since
    // the walk -- simply doesn't appear.
    for (const auto& t : obs.thoughts)
    {
        // Thoughts are written; remarks are said. A remark fires through the same
        // engine but left through a mouth -- it is never notebook material,
        // timed or not.
        if (obs.fired.count(t.id) == 0 || t.isRemark())
            continue;
        const auto it = rec.at.find(t.id);
        out.push_back(Entry{&t, it == rec.at.end() ? kUntimed : it->second});
    }
    // By the moment he wrote it. stable_sort so untimed notes (all equal to each other)
    // keep authored order rather than shuffling between frames.
    std::stable_sort(out.begin(), out.end(),
                     [](const Entry& a, const Entry& b) { return momentBefore(a.at, b.at); });
    return out;
}

std::vector<Day> byDay(const Record& rec, const psyche::State& obs,
                       const worldclock::WorldClock& clock)
{
    std::vector<Day> days;
    for (const auto& e : found(rec, obs)) // already moment-ordered
    {
        const int d = timed(e) ? worldclock::dayAt(clock, e.at) : 0;
        if (days.empty() || days.back().day != d)
            days.push_back(Day{d, {}});
        days.back().entries.push_back(e);
    }
    return days;
}

} // namespace notebook
