#include "Structures.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace structures
{
namespace
{
// Read a [col,row] 2-int cell from the tiles object. A 9-slice structure needs all nine
// keys; a missing/malformed one falls back to {0,0} -- but {0,0} is a REAL atlas cell
// (top-left), so a typo would silently stamp the wrong tile. Log the gap (id + slice) so
// an authoring mistake is loud, not invisible-wrong.
std::array<int, 2> readCell(const nlohmann::json& tiles, const char* key, const std::string& id)
{
    if (const auto it = tiles.find(key); it != tiles.end() && it->is_array() && it->size() == 2)
        return {(*it)[0].get<int>(), (*it)[1].get<int>()};
    std::fprintf(stderr, "[structures] '%s' is missing tile slice '%s' -> defaulting to (0,0)\n",
                 id.c_str(), key);
    return {0, 0};
}

// Read a boolean walkable flag for a slice, defaulting to true (all-footing) if absent.
bool readWalk(const nlohmann::json* walk, const char* key)
{
    if (!walk)
        return true;
    if (const auto it = walk->find(key); it != walk->end() && it->is_boolean())
        return it->get<bool>();
    return true;
}
} // namespace

void load(Config& cfg, const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;

    const auto structs = j.find("structures");
    if (structs == j.end() || !structs->is_object())
        return;
    for (const auto& [id, spec] : structs->items())
    {
        Layout lay;
        lay.surface = spec.value("surface", std::string{});
        const auto tiles = spec.find("tiles");
        if (tiles == spec.end() || !tiles->is_object())
            continue;
        // 3x3 grid: row 0 = top (nw,n,ne), 1 = middle (w,c,e), 2 = bottom (sw,s,se).
        lay.cell[0] = {readCell(*tiles, "nw", id), readCell(*tiles, "n", id),
                       readCell(*tiles, "ne", id)};
        lay.cell[1] = {readCell(*tiles, "w", id), readCell(*tiles, "c", id),
                       readCell(*tiles, "e", id)};
        lay.cell[2] = {readCell(*tiles, "sw", id), readCell(*tiles, "s", id),
                       readCell(*tiles, "se", id)};
        // Optional per-slice walkability (defaults all-true): false = cosmetic overhang.
        const auto wj = spec.find("walkable");
        const nlohmann::json* walk = (wj != spec.end() && wj->is_object()) ? &*wj : nullptr;
        lay.walk[0] = {readWalk(walk, "nw"), readWalk(walk, "n"), readWalk(walk, "ne")};
        lay.walk[1] = {readWalk(walk, "w"), readWalk(walk, "c"), readWalk(walk, "e")};
        lay.walk[2] = {readWalk(walk, "sw"), readWalk(walk, "s"), readWalk(walk, "se")};
        cfg.layouts[id] = lay;
    }
}

} // namespace structures
