#pragma once

#include "ecs/GameComponents.h"
#include "ecs/ItemConfig.h"

#include <string>

// ---------------------------------------------------------------------------
// InventoryOps - pure data-manipulation utilities over Inventory + Equipment.
//
// Ported from games/prison-escape-game/include/ops/InventoryOps.h. Selva
// drops the weapon-evolution operations (canEvolve / evolveWeapon) which
// depend on a Weapon component and an EvolutionRegistry that don't exist
// on Selva's side. The core (slot lookup, equip/unequip, addItem with
// stacking, removeItem with reindex, consume by config_path) is identical.
//
// All functions are pure with respect to the Inventory and Equipment they
// receive - no global state, no side effects beyond the arguments. Same as
// prison-escape's pattern.
// ---------------------------------------------------------------------------

namespace selva::InventoryOps
{

// Returns a mutable reference to the slot index field for the given slot.
int& slotIndex(Equipment& equip, EquipSlot slot);

// Returns the current index value of the given slot (-1 if empty).
int slotIndexConst(const Equipment& equip, EquipSlot slot);

// Returns a pointer to the ItemInstance currently equipped in the slot,
// or nullptr if empty / out-of-range.
const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot);

// Mutable variant of equippedItem.
ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot);

// Returns the config_path of the item equipped in the slot, or "" if empty.
std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot);

// True if the slot has no item equipped.
bool slotEmpty(const Equipment& equip, EquipSlot slot);

// True if the inventory index is currently equipped in any slot.
bool isEquipped(const Equipment& equip, int inv_index);

// Returns the slot the given inventory index is equipped in, or
// EquipSlot::RightHand as a fallback if not equipped (caller should check
// isEquipped first).
EquipSlot equippedInSlot(const Equipment& equip, int inv_index);

// Add an item to the inventory. Stacks into existing entries if the
// ItemDef declares stackable + the existing stack has room; otherwise
// appends a new entry. Returns false if there's no room (inventory full
// and no stacking possible).
bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Remove the item at the given inventory index. Adjusts all equipment
// slot indices to match the reindexed inventory. Returns false if the
// index is out of range.
bool removeItem(Inventory& inv, Equipment& equip, int index);

// Equip the inventory index to the given slot. If the same index is
// already equipped in another slot, that slot is unequipped first.
bool equipItemToSlot(Equipment& equip, int inv_index, EquipSlot slot);

// Set the slot to empty (-1).
void unequipSlot(Equipment& equip, EquipSlot slot);

// Total quantity of items in the inventory with the given config_path
// (summing across stacks).
int countItem(const Inventory& inv, const std::string& config_path);

// Consume `qty` of the given item from the inventory, removing entries
// whose quantity hits zero. Returns true if the full quantity was
// consumed; false if the inventory didn't have enough.
bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty);

} // namespace selva::InventoryOps
