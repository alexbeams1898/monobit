#pragma once

#include "Observations.h" // LineKind

#include <string>
#include <vector>

// The notebook's record -- the dated log of what the pilgrim has come to know.
// Both observations (what he perceived) and thoughts (what he realized) are written
// here on FIRST occurrence, grouped by the in-world day. Kept SEPARATE from the
// notebook item instance (the item is a small handle/gate; this is the growing
// record). Writing into it is gated on carrying the notebook; the per-entry day is
// captured only if he carries a watch (else 0 = undated). See docs/design/
// INVENTORY.md + NOTEBOOK.md.
namespace notebook
{

// One written entry -- a snapshot of a reading at the moment it was first recorded.
struct Entry
{
    observations::LineKind kind = observations::LineKind::Observation;
    std::string text;
    std::string faculty; // thought only (empty for an observation)
    int difficulty = 0;  // thought only (0 for an observation)
    int day = 0;         // in-world day it was written; 0 = undated (no watch)
};

// The record: entries in the order they were written (oldest first). Grouping by
// day is a read-time view over `entries`, not stored -- so nothing to keep in sync.
struct Record
{
    std::vector<Entry> entries;
};

// Append an entry. `day` is the in-world day (0 = undated). Callers gate on the
// notebook being carried and pass day 0 when no watch is carried.
void record(Record& rec, observations::LineKind kind, const std::string& text,
            const std::string& faculty, int difficulty, int day);

// A day's worth of entries, for the notebook view. `day` 0 = the undated group
// (entries written without a watch).
struct DayGroup
{
    int day = 0;
    std::vector<Entry> entries;
};

// Group the record's entries by day for display, preserving write order within a
// day. Undated entries (day 0) form their own group. Days appear in ascending
// order (undated last, as "when unknown"). A read-time view -- nothing stored.
std::vector<DayGroup> groupByDay(const Record& rec);

} // namespace notebook
