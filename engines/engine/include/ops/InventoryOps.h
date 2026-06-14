#pragma once

#include "ecs/Items.h"
#include "ecs/RpgComponents.h"

#include <string>
#include <vector>

// Inventory + Equipment ops. Pure logic on engine::ecs::Inventory /
// Equipment / ItemRegistry / Weapon.
//
// Inventory is category-bucketed (Inventory.by_category). Items are
// addressed by stable ItemInstanceId, allocated monotonically per
// inventory and never reused on remove. Equipment slots hold ids, not
// indices -- any inventory mutation leaves equipment refs valid.

namespace engine::ops::inventory
{

using engine::ecs::Equipment;
using engine::ecs::EquipSlot;
using engine::ecs::EvolutionPath;
using engine::ecs::Inventory;
using engine::ecs::ItemInstance;
using engine::ecs::ItemInstanceId;
using engine::ecs::ItemRegistry;
using engine::ecs::kInvalidItemInstanceId;
using engine::ecs::Weapon;

// --- Slot accessors ----------------------------------------------------

ItemInstanceId& slotId(Equipment& equip, EquipSlot slot);
ItemInstanceId slotIdConst(const Equipment& equip, EquipSlot slot);

// --- Inventory lookup --------------------------------------------------

// Locate an item by its stable id across all category buckets. Returns
// nullptr if no such id exists in this inventory.
const ItemInstance* findById(const Inventory& inv, ItemInstanceId id);
ItemInstance* findByIdMut(Inventory& inv, ItemInstanceId id);

// Resolve the equipped item in `slot`. nullptr if slot is empty or holds
// a stale id (shouldn't happen given the id-stability contract, but is
// the defensive fallback).
const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot);
ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot);

// Config path of the equipped item; empty string if nothing equipped.
std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot);

// --- Equipment queries -------------------------------------------------

bool slotEmpty(const Equipment& equip, EquipSlot slot);
bool isEquipped(const Equipment& equip, ItemInstanceId id);
EquipSlot equippedInSlot(const Equipment& equip, ItemInstanceId id);

// --- Add / remove ------------------------------------------------------

// Add an item. The category is read from `registry` via item.config_path
// and the item is bucketed accordingly. Stackable items merge into
// existing stacks (respecting max_stack); leftover quantity appends as
// a new stack. Newly-appended stacks receive a fresh stable id from
// inv.next_id. The id of an existing stack the new item merged INTO is
// returned via `merged_into_id` (set to kInvalidItemInstanceId for a
// pure-append). Returns the id of the resulting (or final-merged) item;
// kInvalidItemInstanceId on failure (unknown config_path).
//
// Note: item.id is IGNORED on input -- the inventory allocates a fresh
// id. To preserve an id across re-add (e.g. save-load), use addWithId.
ItemInstanceId addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Add an item preserving an explicit id (load-from-save path). Skips
// stackable-merge logic -- the saved instance is reinstated verbatim.
// Advances inv.next_id past the supplied id if needed so future adds
// don't collide.
void addWithId(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Remove the item with this id from inventory. If the item is equipped,
// the equipping slot is cleared. Returns true if the item existed.
bool removeItem(Inventory& inv, Equipment& equip, ItemInstanceId id);

// --- Equip -------------------------------------------------------------

// Equip the item with this id to `slot`. If the same item is equipped
// to another slot, that other slot is cleared first (no double-equip).
// Returns false if id doesn't resolve to any item in the inventory.
bool equipItemToSlot(const Inventory& inv, Equipment& equip, ItemInstanceId id, EquipSlot slot);
void unequipSlot(Equipment& equip, EquipSlot slot);

// --- Stack ops ---------------------------------------------------------

// Total quantity across all stacks of the item with this config_path.
int countItem(const Inventory& inv, const std::string& config_path);

// Consume `qty` of the item with this config_path. Walks stacks in
// reverse insertion order (LIFO). Removes stacks emptied to 0, clears
// equipment refs to removed stacks. Returns true if the full qty was
// consumed; false if inventory had insufficient quantity (partial
// consumption still committed -- caller checks the return).
bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty);

// --- Evolution ---------------------------------------------------------

bool canEvolve(const Inventory& inv, const Equipment& equip, const Weapon& weapon,
               const EvolutionPath& path);

bool evolveWeapon(Inventory& inv, Equipment& equip, Weapon& weapon, const EvolutionPath& path,
                  const std::string& new_weapon_config, const ItemRegistry& registry,
                  float carry_factor, bool free_materials = false);

} // namespace engine::ops::inventory
