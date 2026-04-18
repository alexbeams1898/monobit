#pragma once

#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

namespace InventoryOps
{

// Get a mutable reference to the Equipment slot index for a given EquipSlot.
int& slotIndex(Equipment& equip, EquipSlot slot);

// Get the inventory index for a given slot (read-only).
int slotIndexConst(const Equipment& equip, EquipSlot slot);

// Get the equipped item for a slot, or nullptr if nothing is equipped.
const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot);

// Get mutable equipped item for a slot, or nullptr.
ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot);

// Get the config_path of the equipped item in a slot, or empty string.
std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot);

// Check if a slot has nothing equipped.
bool slotEmpty(const Equipment& equip, EquipSlot slot);

// Check if an inventory index is equipped in any slot.
bool isEquipped(const Equipment& equip, int inv_index);

// Which slot (if any) has this inventory index equipped?
EquipSlot equippedInSlot(const Equipment& equip, int inv_index);

// Add an item to inventory. Stackable items merge with existing stacks.
bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Remove the item at the given index. Returns false if index is out of range.
// Adjusts all equipment slot indices that pointed at or after the removed index.
bool removeItem(Inventory& inv, Equipment& equip, int index);

// Equip the item at inv_index into a specific target slot.
// If the slot already has something, it's just unequipped (stays in inventory).
bool equipItemToSlot(Equipment& equip, int inv_index, EquipSlot slot);

// Unequip the given slot (sets index to -1).
void unequipSlot(Equipment& equip, EquipSlot slot);

// Check if the player can evolve their currently equipped weapon along a given path.
bool canEvolve(const Inventory& inv, const Equipment& equip, const Weapon& weapon,
               const EvolutionPath& path);

// Execute weapon evolution: consume materials, replace equipped weapon, reset weapon XP.
bool evolveWeapon(Inventory& inv, Equipment& equip, Weapon& weapon, const EvolutionPath& path,
                  const std::string& new_weapon_config, const ItemRegistry& registry,
                  float carry_factor, bool free_materials = false);

// Count total quantity of items with a given config_path in inventory.
int countItem(const Inventory& inv, const std::string& config_path);

// Consume qty of items with config_path from inventory. Returns false if insufficient.
// Adjusts equipment indices for any removed inventory entries.
bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty);

} // namespace InventoryOps
