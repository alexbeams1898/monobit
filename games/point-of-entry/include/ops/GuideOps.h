#pragma once

#include <string>
#include <vector>

// THE FIELD GUIDE's reading half. A page's printed sections ride the creature file itself (an
// optional "guide" object), so a species and its page can never split; how much of a page is
// shown is a pure function of the record's kill tally against thresholds in stats.json. The
// screen draws this; record:: counts; nothing here keeps a tally of its own.
namespace guide
{

// The trade's entry format, in its printed order.
struct Entry
{
    std::string description;
    std::string similar;
    std::string biology;
    std::string habits;
    std::string signs;
    std::string control;
};

struct Page
{
    std::string species;  // the creature file path -- the record's id for this species
    std::string name;     // the file stem, the listing's label
    std::string sprite;   // the species' sprite def path -- the page's plate is the same art
    bool printed = false; // the file authors a guide object: the book knows this one
    Entry entry;
    // The species' sheet, for the line the deepest threshold unlocks.
    int resistance = 0;
    int defensiveness = 0;
    int dispersal = 0;
};

// Kill counts at which a page opens up, from the "guide" block of the stats config.
struct Gates
{
    int kills_for_entry = 1;
    int kills_for_stats = 10;
};

// How much of a page the record has earned.
enum class Tier
{
    Undocumented, // the page shows nothing
    Entry,        // the printed sections
    Stats         // the sections plus the sheet line
};

Page load(const std::string& path);

// The listing's label for a species id -- the creature file's stem. One derivation, shared by
// every surface that names a species, so a label can never drift from its page.
std::string nameOf(const std::string& species);

// A species' number in the book, from its place in the scan. Padded, because a register whose
// numbers change width is a register whose names do not line up.
std::string number(int index);

// Every creature file in dir, sorted by path so the listing never reshuffles. Species ids come
// out with forward slashes -- the exact string the swarm spawns by, so the record's lookups
// can never miss on a separator.
std::vector<Page> scan(const std::string& dir);

Gates gates(const std::string& statsPath);

Tier tier(int kills, const Gates& g);

} // namespace guide
