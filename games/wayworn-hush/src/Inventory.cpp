#include "Inventory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace inventory
{
namespace
{
Category categoryFrom(const std::string& s)
{
    if (s == "practical")
        return Category::Practical;
    if (s == "key" || s == "key_item")
        return Category::KeyItem;
    return Category::Keepsake; // default / "keepsake"
}

// Parse one item JSON into a def. `stem` is the filename stem, used as the id when
// the JSON omits an explicit "id".
ItemDef parseDef(const nlohmann::json& j, const std::string& stem)
{
    ItemDef d;
    d.id = j.value("id", stem);
    d.name = j.value("name", d.id);
    d.description = j.value("description", std::string{});
    d.icon = j.value("icon", std::string{});
    d.icon_size = j.value("icon_size", d.icon_size);
    // A cell on the shared items sheet: {"icon_cell": [col, row]}. Absent -> the
    // whole image is the art (a loose per-item PNG).
    if (const auto it = j.find("icon_cell"); it != j.end() && it->is_array() && it->size() == 2)
    {
        d.icon_col = (*it)[0].get<int>();
        d.icon_row = (*it)[1].get<int>();
    }
    d.category = categoryFrom(j.value("category", std::string{"keepsake"}));
    d.rarity = j.value("rarity", 1);
    d.max_stack = std::max(1, j.value("max_stack", 99));
    return d;
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
        ItemDef d = parseDef(j, entry.path().stem().string());
        if (!d.id.empty())
            out.defs[d.id] = std::move(d);
    }
}

void add(Satchel& satchel, const Registry& registry, ItemInstance item)
{
    if (item.id.empty() || item.quantity <= 0)
        return;

    // Every item stacks: fill existing stacks of this id up to its cap, then spill the remainder
    // into fresh stacks so no entry exceeds max_stack. An unknown id (no def) uses cap 1.
    const ItemDef* def = registry.find(item.id);
    const int cap = def ? std::max(1, def->max_stack) : 1;
    for (auto& e : satchel.items)
    {
        if (item.quantity <= 0)
            return;
        if (e.id != item.id || e.quantity >= cap)
            continue;
        const int room = cap - e.quantity;
        const int move = std::min(room, item.quantity);
        e.quantity += move;
        item.quantity -= move;
    }
    while (item.quantity > 0)
    {
        ItemInstance stack = item;
        stack.quantity = std::min(cap, item.quantity);
        item.quantity -= stack.quantity;
        satchel.items.push_back(std::move(stack));
    }
}

bool remove(Satchel& satchel, const std::string& id, int qty)
{
    if (qty <= 0)
        return true;
    if (count(satchel, id) < qty)
        return false; // all-or-nothing: don't partially consume on a failed cost

    int remaining = qty;
    for (auto& e : satchel.items)
    {
        if (remaining <= 0)
            break;
        if (e.id != id)
            continue;
        const int take = std::min(e.quantity, remaining);
        e.quantity -= take;
        remaining -= take;
    }
    satchel.items.erase(std::remove_if(satchel.items.begin(), satchel.items.end(),
                                       [](const ItemInstance& e) { return e.quantity <= 0; }),
                        satchel.items.end());
    return true;
}

int count(const Satchel& satchel, const std::string& id)
{
    int n = 0;
    for (const auto& e : satchel.items)
        if (e.id == id)
            n += e.quantity;
    return n;
}

bool has(const Satchel& satchel, const std::string& id)
{
    return count(satchel, id) > 0;
}

void markAllSeen(Satchel& satchel)
{
    for (auto& e : satchel.items)
        e.is_new = false;
}

} // namespace inventory
