#include "ops/InventoryOps.h"

namespace engine::ops::inventory
{

using engine::ecs::ItemDef;

int& slotIndex(Equipment& equip, EquipSlot slot)
{
    switch (slot)
    {
    case EquipSlot::RightHand:
        return equip.right_hand;
    case EquipSlot::LeftHand:
        return equip.left_hand;
    case EquipSlot::Head:
        return equip.head;
    case EquipSlot::Chest:
        return equip.chest;
    case EquipSlot::Legs:
        return equip.legs;
    case EquipSlot::Feet:
        return equip.feet;
    case EquipSlot::Accessory1:
        return equip.accessory_1;
    case EquipSlot::Accessory2:
    default:
        return equip.accessory_2;
    }
}

int slotIndexConst(const Equipment& equip, EquipSlot slot)
{
    switch (slot)
    {
    case EquipSlot::RightHand:
        return equip.right_hand;
    case EquipSlot::LeftHand:
        return equip.left_hand;
    case EquipSlot::Head:
        return equip.head;
    case EquipSlot::Chest:
        return equip.chest;
    case EquipSlot::Legs:
        return equip.legs;
    case EquipSlot::Feet:
        return equip.feet;
    case EquipSlot::Accessory1:
        return equip.accessory_1;
    case EquipSlot::Accessory2:
    default:
        return equip.accessory_2;
    }
}

const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    const int idx = slotIndexConst(equip, slot);
    if (idx < 0 || idx >= static_cast<int>(inv.items.size()))
        return nullptr;
    return &inv.items[idx];
}

ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    const int idx = slotIndexConst(equip, slot);
    if (idx < 0 || idx >= static_cast<int>(inv.items.size()))
        return nullptr;
    return &inv.items[idx];
}

std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    const auto* item = equippedItem(inv, equip, slot);
    return item != nullptr ? item->config_path : std::string{};
}

bool slotEmpty(const Equipment& equip, EquipSlot slot)
{
    return slotIndexConst(equip, slot) < 0;
}

namespace
{
constexpr EquipSlot ALL_SLOTS[] = {EquipSlot::RightHand,  EquipSlot::LeftHand,  EquipSlot::Head,
                                   EquipSlot::Chest,      EquipSlot::Legs,      EquipSlot::Feet,
                                   EquipSlot::Accessory1, EquipSlot::Accessory2};

// Adjust all equipment indices after an inventory removal at `removed`.
void adjustIndicesAfterRemoval(Equipment& equip, int removed)
{
    for (const auto s : ALL_SLOTS)
    {
        int& idx = slotIndex(equip, s);
        if (idx == removed)
            idx = -1;
        else if (idx > removed)
            --idx;
    }
}
} // namespace

bool isEquipped(const Equipment& equip, int inv_index)
{
    if (inv_index < 0)
        return false;
    for (const auto s : ALL_SLOTS)
    {
        if (slotIndexConst(equip, s) == inv_index)
            return true;
    }
    return false;
}

EquipSlot equippedInSlot(const Equipment& equip, int inv_index)
{
    for (const auto s : ALL_SLOTS)
    {
        if (slotIndexConst(equip, s) == inv_index)
            return s;
    }
    return EquipSlot::RightHand;
}

bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry)
{
    const ItemDef* def = registry.find(item.config_path);
    const bool stackable = def != nullptr && def->stackable;
    const int max_stack = (def != nullptr) ? def->max_stack : 1;

    int remaining = item.quantity;

    if (stackable)
    {
        for (auto& existing : inv.items)
        {
            if (remaining <= 0)
                break;
            if (existing.config_path == item.config_path && existing.quantity < max_stack)
            {
                const int space = max_stack - existing.quantity;
                const int to_add = (remaining <= space) ? remaining : space;
                existing.quantity += to_add;
                remaining -= to_add;
            }
        }
        if (remaining <= 0)
            return true;
    }

    if (static_cast<int>(inv.items.size()) >= inv.max_slots)
        return remaining <= 0;

    ItemInstance remainder = item;
    remainder.quantity = remaining;
    inv.items.push_back(remainder);
    return true;
}

bool removeItem(Inventory& inv, Equipment& equip, int index)
{
    if (index < 0 || index >= static_cast<int>(inv.items.size()))
        return false;

    inv.items.erase(inv.items.begin() + index);
    adjustIndicesAfterRemoval(equip, index);
    return true;
}

bool equipItemToSlot(Equipment& equip, int inv_index, EquipSlot slot)
{
    if (inv_index < 0)
        return false;

    // If this item is already equipped in another slot, unequip it there first.
    for (const auto s : ALL_SLOTS)
    {
        if (s != slot && slotIndexConst(equip, s) == inv_index)
            slotIndex(equip, s) = -1;
    }

    slotIndex(equip, slot) = inv_index;
    return true;
}

void unequipSlot(Equipment& equip, EquipSlot slot)
{
    slotIndex(equip, slot) = -1;
}

int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& item : inv.items)
    {
        if (item.config_path == config_path)
            total += item.quantity;
    }
    return total;
}

bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty)
{
    int remaining = qty;
    for (int i = static_cast<int>(inv.items.size()) - 1; i >= 0 && remaining > 0; --i)
    {
        if (inv.items[i].config_path != config_path)
            continue;

        if (inv.items[i].quantity <= remaining)
        {
            remaining -= inv.items[i].quantity;
            inv.items.erase(inv.items.begin() + i);
            adjustIndicesAfterRemoval(equip, i);
        }
        else
        {
            inv.items[i].quantity -= remaining;
            remaining = 0;
        }
    }
    return remaining <= 0;
}

bool canEvolve(const Inventory& inv, const Equipment& equip, const Weapon& weapon,
               const EvolutionPath& path)
{
    if (slotEmpty(equip, EquipSlot::RightHand))
        return false;
    if (weapon.wxp_level < path.min_level)
        return false;
    if (!path.material_config_path.empty())
    {
        if (countItem(inv, path.material_config_path) < path.material_qty)
            return false;
    }
    return true;
}

bool evolveWeapon(Inventory& inv, Equipment& equip, Weapon& weapon, const EvolutionPath& path,
                  const std::string& new_weapon_config, const ItemRegistry& /*registry*/,
                  float carry_factor, bool free_materials)
{
    if (!canEvolve(inv, equip, weapon, path))
        return false;

    if (!free_materials && !path.material_config_path.empty())
    {
        if (!consumeItems(inv, equip, path.material_config_path, path.material_qty))
            return false;
    }

    auto* wpn = equippedItemMut(inv, equip, EquipSlot::RightHand);
    if (wpn == nullptr)
        return false;

    const float bonus = wpn->evolution_bonus + static_cast<float>(weapon.wxp_level) * carry_factor;

    wpn->config_path = new_weapon_config;
    wpn->evolution_bonus = bonus;
    wpn->newly_discovered = true;

    weapon.wxp_level = 1;
    weapon.wxp_current = 0.0f;
    weapon.wxp_to_next = 100.0f;

    return true;
}

} // namespace engine::ops::inventory
