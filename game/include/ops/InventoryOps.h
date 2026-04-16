#pragma once

#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

namespace InventoryOps
{

// Add an item to inventory. Stackable items merge with existing stacks.
// Returns true if added, false if inventory full or stack full.
bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Remove the item at the given index. Returns false if index is out of range.
bool removeItem(Inventory& inv, int index);

// Equip the item at inv_index into the appropriate slot. If the slot is
// occupied, the old item is swapped back into the inventory at the same index.
// Returns false if index is invalid or category doesn't fit a slot.
bool equipItem(Inventory& inv, Equipment& equip, int inv_index, const ItemRegistry& registry);

// Equip the item at inv_index into a specific target slot. Used when the UI
// lets the player choose which hand to equip into (weapons and shields can
// go in either hand).
bool equipItemToSlot(Inventory& inv, Equipment& equip, int inv_index, EquipSlot slot,
                     const ItemRegistry& registry);

// Unequip the given slot back into inventory.
// Returns false if slot is empty or inventory is full.
bool unequipSlot(Inventory& inv, Equipment& equip, EquipSlot slot);

// Get a mutable reference to the Equipment slot for a given EquipSlot enum.
ItemInstance& slotRef(Equipment& equip, EquipSlot slot);

// Get a const reference to the Equipment slot for a given EquipSlot enum.
const ItemInstance& slotRef(const Equipment& equip, EquipSlot slot);

// Check if the player can evolve their currently equipped weapon along a given path.
// Returns false if: weapon level too low, missing materials, or inventory full.
bool canEvolve(const Inventory& inv, const Equipment& equip, const WeaponXP& wxp,
               const EvolutionPath& path);

// Execute weapon evolution: consume materials, replace equipped weapon, reset weapon XP
// with carry-forward bonus. If free_materials is true, materials are not consumed (god mode).
bool evolveWeapon(Inventory& inv, Equipment& equip, WeaponXP& wxp, const EvolutionPath& path,
                  const std::string& new_weapon_config, const ItemRegistry& registry,
                  float carry_factor, bool free_materials = false);

// Count total quantity of items with a given config_path in inventory.
int countItem(const Inventory& inv, const std::string& config_path);

// Consume qty of items with config_path from inventory. Returns false if insufficient.
bool consumeItems(Inventory& inv, const std::string& config_path, int qty);

} // namespace InventoryOps
