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

// Unequip the given slot back into inventory.
// Returns false if slot is empty or inventory is full.
bool unequipSlot(Inventory& inv, Equipment& equip, EquipSlot slot);

// Get a mutable reference to the Equipment slot for a given EquipSlot enum.
ItemInstance& slotRef(Equipment& equip, EquipSlot slot);

// Get a const reference to the Equipment slot for a given EquipSlot enum.
const ItemInstance& slotRef(const Equipment& equip, EquipSlot slot);

} // namespace InventoryOps
