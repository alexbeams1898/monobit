#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// The pilgrim's satchel -- "things carried." Deliberately light (DESIGN.md: no
// weight, no slot-Tetris): an unbounded, unordered bag. Two layers, like the
// engine's other authored data -- an immutable ItemDef (blueprint, one JSON per
// item) resolved from an ItemRegistry, and a lightweight ItemInstance (a copy in
// the bag). Key items (notebook, watch, gating gear) are queried by has(); their
// "carried effect" is the caller checking has() and acting. See docs/design/
// INVENTORY.md.
namespace inventory
{

// The three roles the fiction names (DESIGN.md "Gathering / inventory"):
//   Practical -- ingredients / crafting materials. Stackable, consumed.
//   Keepsake  -- collection / attention-reward. Kept, no mechanical use (yet).
//   KeyItem   -- notebook, watch, gating gear, skill-unlockers. Unique, never
//                consumed, queried by has(); may grant a carried effect.
enum class Category
{
    Practical,
    Keepsake,
    KeyItem
};

// Immutable blueprint, authored one-JSON-per-item, keyed by a short stable id.
// Small by design -- no category-specific kitchen sink; add a sub-struct if a
// category ever needs its own data.
struct ItemDef
{
    std::string id;          // "river_stone", "notebook", "wild_thyme"
    std::string name;        // display name
    std::string description; // flavor / what it is
    std::string icon;        // sprite path (placeholder ok)
    Category category = Category::Keepsake;
    // Per-TYPE rarity on the SAME 1..5 scale as reading difficulty (reuse
    // reading_color::rarityWord/rarityColor). 0 = no rarity label.
    int rarity = 1;
    bool stackable = false; // Practical stacks; Keepsake/KeyItem do not
    int max_stack = 1;
};

// A concrete copy in the satchel.
struct ItemInstance
{
    std::string id;     // -> ItemDef
    int quantity = 1;   // for stackables
    bool is_new = true; // "newly found" badge for the UI (mirrors reading is_new)
};

// The player's bag: a plain vector, no cap.
struct Satchel
{
    std::vector<ItemInstance> items;
};

// The loaded blueprints, keyed by id.
struct Registry
{
    std::unordered_map<std::string, ItemDef> defs;

    const ItemDef* find(const std::string& id) const
    {
        const auto it = defs.find(id);
        return it != defs.end() ? &it->second : nullptr;
    }
};

// Load all item blueprints from a directory of JSON files (one per item). The id
// is the JSON's "id" field, or the filename stem if absent. Silent no-op if the
// directory is missing. Call once at startup.
void load(Registry& out, const std::string& dir);

// Add an instance to the satchel. If its def is stackable, merges into an existing
// stack up to max_stack (spilling into new stacks past the cap); otherwise appends
// a distinct entry. Needs the registry to know stackability + max_stack.
void add(Satchel& satchel, const Registry& registry, ItemInstance item);

// Remove up to `qty` of an item (across stacks). Returns false and removes nothing
// if fewer than `qty` are present (callers gate on the return, e.g. crafting cost).
bool remove(Satchel& satchel, const std::string& id, int qty = 1);

// Total quantity of an item across all its stacks.
int count(const Satchel& satchel, const std::string& id);

// Clear the "newly found" flag on every carried item (they've been seen). Called when the
// player leaves the satchel view.
void markAllSeen(Satchel& satchel);

// Whether the pilgrim carries at least one. The gating query -- item-gating and
// key-item carried-effects read this everywhere (has(satchel, "notebook")).
bool has(const Satchel& satchel, const std::string& id);

} // namespace inventory
