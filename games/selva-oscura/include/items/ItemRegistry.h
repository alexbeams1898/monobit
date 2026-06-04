#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace selva::items
{

// Which carrier-side struct represents an instance of this item.
// Drives the per-entry storage in selva::items::Inventory (Layer 3).
enum class EntryKind
{
    Possession, // single id; you have it or don't (Grimoire, Cord)
    Stack,      // id + count (consumables, materials)
    Instanced,  // id + per-instance state (weapons, armor)
};

const char* entryKindName(EntryKind k);
EntryKind parseEntryKind(const std::string& s);

// One item definition. Loaded from config/items/<id>.json. Static data;
// per-player ownership lives on PlayerProfile.inventory (Layer 3).
struct ItemDef
{
    std::string id;           // unique key, matches filename stem
    std::string display_name; // UI label
    std::string description;  // long-form text shown in detail panel
    std::string category;     // foreign key into CategoryRegistry
    EntryKind kind = EntryKind::Possession;
    std::string icon_path;     // assets path to icon texture (empty = no icon)
    std::string use_handler;   // C++ handler key; empty = no Use action
    std::string use_condition; // C++ handler key; empty = always usable
};

class ItemRegistry
{
  public:
    // Load every *.json in `dir` as an ItemDef. Validates each item's
    // category against the CategoryRegistry; items referencing an unknown
    // category are logged and skipped (loud failure at boot, not at
    // use time).
    void loadDirectory(const std::filesystem::path& dir);

    // nullptr if no item with that id is loaded.
    const ItemDef* get(const std::string& id) const;

    const std::unordered_map<std::string, ItemDef>& all() const
    {
        return by_id;
    }

  private:
    std::unordered_map<std::string, ItemDef> by_id;
};

ItemRegistry& itemRegistry();

} // namespace selva::items
