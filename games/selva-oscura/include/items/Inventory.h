#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace selva::items
{

// Possession kind: just an id. Used for unique single-instance items
// (Seal, Grimoire, Cord).
struct PossessionEntry
{
    std::string item_id;
};

// Stack kind: id + count. Used for consumables, materials.
struct StackEntry
{
    std::string item_id;
    int count = 0;
};

// Instanced kind: id + per-instance state. Used for weapons / armor
// where two of the same item can have different upgrade levels.
struct InstancedEntry
{
    std::string item_id;
    int upgrade_level = 0;
};

// Polymorphic entry. Variant alternative is fixed by the item's
// ItemDef.kind (PossessionEntry for kind=possession, etc.). Type-safe
// access via std::get_if or std::visit.
using Entry = std::variant<PossessionEntry, StackEntry, InstancedEntry>;

// Inventory: category_id -> ordered entries in that category.
// Categories with no entries are simply absent from the map. Renaming
// a category is a JSON-only edit; the map keys follow the JSON ids.
struct Inventory
{
    std::unordered_map<std::string, std::vector<Entry>> by_category;
};

} // namespace selva::items
