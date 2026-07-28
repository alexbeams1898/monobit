#include "Loot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace loot
{
namespace
{
Entry parseEntry(const nlohmann::json& j)
{
    Entry e;
    e.item = j.value("item", std::string{});
    e.weight = std::max(0, j.value("weight", 1));
    if (const auto qty = j.find("qty"); qty != j.end() && qty->is_object())
    {
        e.qty_min = std::max(1, qty->value("min", 1));
        e.qty_max = std::max(e.qty_min, qty->value("max", e.qty_min));
    }
    return e;
}

Table parseTable(const nlohmann::json& j, const std::string& fallbackId)
{
    Table t;
    t.id = j.value("id", fallbackId);
    if (const auto rolls = j.find("rolls"); rolls != j.end() && rolls->is_object())
    {
        t.rolls_min = std::max(0, rolls->value("min", 1));
        t.rolls_max = std::max(t.rolls_min, rolls->value("max", t.rolls_min));
    }
    if (const auto entries = j.find("entries"); entries != j.end() && entries->is_array())
        for (const auto& e : *entries)
            if (e.is_object())
            {
                Entry parsed = parseEntry(e);
                if (!parsed.item.empty() && parsed.weight > 0)
                    t.entries.push_back(std::move(parsed));
            }
    return t;
}

// A uniform int in [lo, hi] (inclusive) from the shared [0,n] roll source.
int rollRange(int lo, int hi, const psyche::RollRng& rng)
{
    return hi <= lo ? lo : lo + rng(hi - lo);
}

// Pick one entry by weight: a value in [0, total-1] walks the cumulative weights.
const Entry* pickWeighted(const Table& table, int total, const psyche::RollRng& rng)
{
    int r = rng(total - 1); // [0, total-1]
    for (const auto& e : table.entries)
    {
        r -= e.weight;
        if (r < 0)
            return &e;
    }
    return table.entries.empty() ? nullptr : &table.entries.back(); // rounding guard
}
} // namespace

void load(Registry& out, const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
        return;

    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        std::ifstream f(entry.path());
        if (!f)
            continue;
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object())
            continue;
        Table t = parseTable(j, entry.path().stem().string());
        if (!t.id.empty())
            out.tables[t.id] = std::move(t);
    }
}

std::vector<inventory::ItemInstance> roll(const Table& table, const psyche::RollRng& rng)
{
    std::vector<inventory::ItemInstance> out;
    const int total = table.totalWeight();
    if (total <= 0)
        return out;

    const int rolls = rollRange(table.rolls_min, table.rolls_max, rng);
    for (int i = 0; i < rolls; ++i)
    {
        const Entry* e = pickWeighted(table, total, rng);
        if (!e)
            break;
        out.push_back(inventory::ItemInstance{e->item, rollRange(e->qty_min, e->qty_max, rng)});
    }
    return out;
}

} // namespace loot
