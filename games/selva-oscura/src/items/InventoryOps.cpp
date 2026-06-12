#include "items/InventoryOps.h"

#include "items/ItemRegistry.h"

#include <cstdio>

namespace selva::items
{

namespace
{

const ItemDef* defOf(const std::string& item_id)
{
    return itemRegistry().get(item_id);
}

// categoryVec returns a non-const reference into inv so callers can
// mutate the vector; inv therefore must stay non-const even though
// this function body alone doesn't mutate it.
// cppcheck-suppress constParameterReference
std::vector<Entry>& categoryVec(Inventory& inv, const std::string& category_id)
{
    return inv.by_category[category_id];
}

// Find the first entry in any category whose item_id matches. Returns
// (category_id, index) or ("", -1) if not found.
std::pair<std::string, int> findEntry(const Inventory& inv, const std::string& item_id)
{
    for (const auto& [cat_id, vec] : inv.by_category)
    {
        for (std::size_t i = 0; i < vec.size(); ++i)
        {
            const Entry& e = vec[i];
            const std::string* id = nullptr;
            if (const auto* p = std::get_if<PossessionEntry>(&e))
                id = &p->item_id;
            else if (const auto* s = std::get_if<StackEntry>(&e))
                id = &s->item_id;
            else if (const auto* x = std::get_if<InstancedEntry>(&e))
                id = &x->item_id;
            if (id != nullptr && *id == item_id)
                return {cat_id, static_cast<int>(i)};
        }
    }
    return {std::string{}, -1};
}

} // namespace

bool grant(Inventory& inv, const std::string& item_id)
{
    const ItemDef* def = defOf(item_id);
    if (def == nullptr)
    {
        std::fprintf(stderr, "[inventory] grant: unknown item '%s'\n", item_id.c_str());
        std::fflush(stderr);
        return false;
    }
    if (def->kind != EntryKind::Possession)
    {
        std::fprintf(stderr, "[inventory] grant: '%s' is kind=%s, expected possession\n",
                     item_id.c_str(), entryKindName(def->kind));
        std::fflush(stderr);
        return false;
    }
    if (has(inv, item_id))
        return false;
    categoryVec(inv, def->category).emplace_back(PossessionEntry{item_id});
    return true;
}

int addStack(Inventory& inv, const std::string& item_id, int amount)
{
    const ItemDef* def = defOf(item_id);
    if (def == nullptr)
    {
        std::fprintf(stderr, "[inventory] addStack: unknown item '%s'\n", item_id.c_str());
        std::fflush(stderr);
        return 0;
    }
    if (def->kind != EntryKind::Stack)
    {
        std::fprintf(stderr, "[inventory] addStack: '%s' is kind=%s, expected stack\n",
                     item_id.c_str(), entryKindName(def->kind));
        std::fflush(stderr);
        return 0;
    }
    auto& vec = categoryVec(inv, def->category);
    for (Entry& e : vec)
    {
        if (auto* s = std::get_if<StackEntry>(&e))
        {
            if (s->item_id == item_id)
            {
                s->count += amount;
                return s->count;
            }
        }
    }
    vec.emplace_back(StackEntry{item_id, amount});
    return amount;
}

int removeStack(Inventory& inv, const std::string& item_id, int amount)
{
    const ItemDef* def = defOf(item_id);
    if (def == nullptr)
        return -1;
    if (def->kind != EntryKind::Stack)
    {
        std::fprintf(stderr, "[inventory] removeStack: '%s' is kind=%s, expected stack\n",
                     item_id.c_str(), entryKindName(def->kind));
        std::fflush(stderr);
        return -1;
    }
    auto& vec = categoryVec(inv, def->category);
    for (std::size_t i = 0; i < vec.size(); ++i)
    {
        if (auto* s = std::get_if<StackEntry>(&vec[i]))
        {
            if (s->item_id != item_id)
                continue;
            s->count -= amount;
            const int remaining = s->count;
            if (remaining <= 0)
                vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(i));
            return remaining > 0 ? remaining : 0;
        }
    }
    return -1;
}

// Caller must verify the item exists + is instanced-kind. Returns
// a placeholder static if either check fails (still safe to mutate;
// just doesn't end up in any inventory). Future: redesign to return
// a pointer/optional so callers handle the error path explicitly.
InstancedEntry& addInstanced(Inventory& inv, const std::string& item_id)
{
    static InstancedEntry sFallback;
    sFallback = InstancedEntry{};
    const ItemDef* def = defOf(item_id);
    if (def == nullptr)
    {
        std::fprintf(stderr, "[inventory] addInstanced: unknown item '%s'\n", item_id.c_str());
        std::fflush(stderr);
        return sFallback;
    }
    if (def->kind != EntryKind::Instanced)
    {
        std::fprintf(stderr, "[inventory] addInstanced: '%s' is kind=%s, expected instanced\n",
                     item_id.c_str(), entryKindName(def->kind));
        std::fflush(stderr);
        return sFallback;
    }
    auto& vec = categoryVec(inv, def->category);
    vec.emplace_back(InstancedEntry{item_id, 0});
    return std::get<InstancedEntry>(vec.back());
}

bool has(const Inventory& inv, const std::string& item_id)
{
    return findEntry(inv, item_id).second >= 0;
}

bool remove(Inventory& inv, const std::string& item_id)
{
    auto [cat_id, idx] = findEntry(inv, item_id);
    if (idx < 0)
        return false;
    auto it = inv.by_category.find(cat_id);
    if (it == inv.by_category.end())
        return false;
    auto& vec = it->second;
    // For Stack with count > 1, decrement instead of erasing the entry.
    if (auto* s = std::get_if<StackEntry>(&vec[static_cast<std::size_t>(idx)]))
    {
        if (s->count > 1)
        {
            s->count -= 1;
            return true;
        }
    }
    vec.erase(vec.begin() + static_cast<std::ptrdiff_t>(idx));
    return true;
}

const std::vector<Entry>* entriesIn(const Inventory& inv, const std::string& category_id)
{
    auto it = inv.by_category.find(category_id);
    if (it == inv.by_category.end())
        return nullptr;
    return &it->second;
}

} // namespace selva::items
