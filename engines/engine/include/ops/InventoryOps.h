#pragma once

#include "ecs/Items.h"
#include "ecs/RpgComponents.h"

#include <string>

// Inventory + Equipment ops. Pure logic on engine::ecs::Inventory /
// Equipment / ItemRegistry / Weapon. Ported from PE's InventoryOps with
// the PE-specific `equip.synced_*` lines removed (those are game-side
// sprite-sync caches; engine-layer Equipment doesn't carry them).

namespace engine::ops::inventory
{

using engine::ecs::Equipment;
using engine::ecs::EquipSlot;
using engine::ecs::EvolutionPath;
using engine::ecs::Inventory;
using engine::ecs::ItemInstance;
using engine::ecs::ItemRegistry;
using engine::ecs::Weapon;

int& slotIndex(Equipment& equip, EquipSlot slot);
int slotIndexConst(const Equipment& equip, EquipSlot slot);

const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot);
ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot);

std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot);

bool slotEmpty(const Equipment& equip, EquipSlot slot);
bool isEquipped(const Equipment& equip, int inv_index);
EquipSlot equippedInSlot(const Equipment& equip, int inv_index);

bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry);

// Removes the item at the given index. Adjusts every Equipment slot
// that pointed at or after `index`.
bool removeItem(Inventory& inv, Equipment& equip, int index);

bool equipItemToSlot(Equipment& equip, int inv_index, EquipSlot slot);
void unequipSlot(Equipment& equip, EquipSlot slot);

int countItem(const Inventory& inv, const std::string& config_path);
bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty);

bool canEvolve(const Inventory& inv, const Equipment& equip, const Weapon& weapon,
               const EvolutionPath& path);
bool evolveWeapon(Inventory& inv, Equipment& equip, Weapon& weapon, const EvolutionPath& path,
                  const std::string& new_weapon_config, const ItemRegistry& registry,
                  float carry_factor, bool free_materials = false);

} // namespace engine::ops::inventory
