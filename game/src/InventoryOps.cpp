#include "InventoryOps.h"

namespace InventoryOps
{

ItemInstance& slotRef(Equipment& equip, EquipSlot slot)
{
    switch (slot)
    {
    case EquipSlot::MainHand:
        return equip.main_hand;
    case EquipSlot::OffHand:
        return equip.off_hand;
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

const ItemInstance& slotRef(const Equipment& equip, EquipSlot slot)
{
    switch (slot)
    {
    case EquipSlot::MainHand:
        return equip.main_hand;
    case EquipSlot::OffHand:
        return equip.off_hand;
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

// Determine which EquipSlot an item belongs in based on its definition.
static EquipSlot targetSlot(const ItemDef& def)
{
    if (def.category == ItemCategory::Weapon)
        return EquipSlot::MainHand;

    if (def.category == ItemCategory::Armor)
    {
        if (def.max_guard > 0.0f)
            return EquipSlot::OffHand;

        switch (def.armor_slot)
        {
        case ArmorSlot::Head:
            return EquipSlot::Head;
        case ArmorSlot::Chest:
            return EquipSlot::Chest;
        case ArmorSlot::Legs:
            return EquipSlot::Legs;
        case ArmorSlot::Feet:
            return EquipSlot::Feet;
        }
    }

    // Consumables, key items, materials are not equippable.
    return EquipSlot::MainHand;
}

bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry)
{
    const ItemDef* def = registry.find(item.config_path);
    const bool stackable = def != nullptr && def->stackable;
    const int maxStack = (def != nullptr) ? def->max_stack : 1;

    if (stackable)
    {
        // Try to merge into an existing stack.
        for (auto& existing : inv.items)
        {
            if (existing.config_path == item.config_path && existing.quantity < maxStack)
            {
                int space = maxStack - existing.quantity;
                int toAdd = (item.quantity <= space) ? item.quantity : space;
                existing.quantity += toAdd;
                if (toAdd >= item.quantity)
                    return true;
                // Remainder needs a new slot (handled below).
                // For simplicity, we don't split across multiple stacks here.
            }
        }
    }

    if (static_cast<int>(inv.items.size()) >= inv.max_slots)
        return false;

    inv.items.push_back(item);
    return true;
}

bool removeItem(Inventory& inv, int index)
{
    if (index < 0 || index >= static_cast<int>(inv.items.size()))
        return false;

    inv.items.erase(inv.items.begin() + index);
    return true;
}

bool equipItem(Inventory& inv, Equipment& equip, int inv_index, const ItemRegistry& registry)
{
    if (inv_index < 0 || inv_index >= static_cast<int>(inv.items.size()))
        return false;

    const ItemDef* def = registry.find(inv.items[inv_index].config_path);
    if (def == nullptr)
        return false;

    // Only weapons and armor are equippable.
    if (def->category != ItemCategory::Weapon && def->category != ItemCategory::Armor)
        return false;

    EquipSlot slot = targetSlot(*def);
    ItemInstance& target = slotRef(equip, slot);

    // 2-handed weapon clears off-hand.
    if (slot == EquipSlot::MainHand && def->two_handed && !equip.off_hand.empty())
    {
        if (static_cast<int>(inv.items.size()) >= inv.max_slots)
            return false;
        inv.items.push_back(std::move(equip.off_hand));
        equip.off_hand = {};
    }

    // Swap: move old equipped item back to inventory at the same index.
    ItemInstance incoming = std::move(inv.items[inv_index]);
    if (!target.empty())
    {
        inv.items[inv_index] = std::move(target);
    }
    else
    {
        inv.items.erase(inv.items.begin() + inv_index);
    }

    target = std::move(incoming);

    if (slot == EquipSlot::MainHand)
        equip.two_handing = def->two_handed;

    return true;
}

bool unequipSlot(Inventory& inv, Equipment& equip, EquipSlot slot)
{
    ItemInstance& target = slotRef(equip, slot);
    if (target.empty())
        return false;

    if (static_cast<int>(inv.items.size()) >= inv.max_slots)
        return false;

    inv.items.push_back(std::move(target));
    target = {};

    if (slot == EquipSlot::MainHand)
        equip.two_handing = false;

    return true;
}

} // namespace InventoryOps
