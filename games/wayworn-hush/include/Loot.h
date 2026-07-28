#pragma once

#include "Inventory.h"
#include "Psyche.h" // psyche::RollRng (the shared int(int) roll source)

#include <string>
#include <unordered_map>
#include <vector>

// The gather lottery: a gather node names a loot TABLE, and harvesting it rolls a handful
// of items from that table. Static drops name a single item id instead (an actionable
// interaction::ActionKind::Pickup) and don't touch this system. A table is authored one-
// JSON-per-file (config/loot/*.json), keyed by a stable id. The roll is pure over an RNG so
// it unit-tests without a registry. See docs/design/GAME-SYSTEMS.md.
namespace loot
{

// One possible drop in a table: an item, its relative weight (NOT a percentage -- a roll
// picks an entry proportional to its weight over the table's total), and the quantity
// range for that pick (inclusive).
struct Entry
{
    std::string item; // -> inventory::ItemDef
    int weight = 1;   // relative weight within the table (>0)
    int qty_min = 1;  // inclusive quantity range for this entry
    int qty_max = 1;
};

// A named loot table: N independent weighted picks per harvest (rolls_min..rolls_max),
// each pick drawing one entry by weight and then rolling its quantity. Ranging the roll
// count makes a harvest yield a handful, not a fixed amount.
struct Table
{
    std::string id;
    int rolls_min = 1;
    int rolls_max = 1;
    std::vector<Entry> entries;

    int totalWeight() const
    {
        int sum = 0;
        for (const auto& e : entries)
            sum += e.weight > 0 ? e.weight : 0;
        return sum;
    }
};

// The loaded tables, keyed by id.
struct Registry
{
    std::unordered_map<std::string, Table> tables;

    const Table* find(const std::string& id) const
    {
        const auto it = tables.find(id);
        return it != tables.end() ? &it->second : nullptr;
    }
};

// Load all loot tables from a directory of JSON files (one per table; id = the JSON's "id"
// or the filename stem). Silent no-op if the dir is missing. Call once at startup.
void load(Registry& out, const std::string& dir);

// Roll a table: rolls_min..rolls_max independent weighted picks, each yielding one entry's
// item at a rolled quantity. Pure over `rng` (an int(int) returning a uniform value in
// [0,n]) so a fixed rng gives deterministic output -- the same source the observation rolls
// use. Returns the drawn items (merged is the caller's job via inventory::add). Empty if
// the table is empty / all-zero weight.
std::vector<inventory::ItemInstance> roll(const Table& table, const psyche::RollRng& rng);

} // namespace loot
