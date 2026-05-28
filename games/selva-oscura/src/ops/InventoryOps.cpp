#include "ops/InventoryOps.h"

#include "ecs/ItemConfig.h"

namespace selva
{

// Singleton item-registry storage. Declared here (in the same TU as the
// InventoryOps that consume it) to keep the registry's lifetime tied to
// the ops layer. Same Meyer-style singleton pattern as the other Selva
// registries (selva::anim::clips() etc.).
ItemRegistry& itemRegistry()
{
    static ItemRegistry s_registry;
    return s_registry;
}

} // namespace selva

namespace selva::InventoryOps
{

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
    return &inv.items[static_cast<std::size_t>(idx)];
}

ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    const int idx = slotIndexConst(equip, slot);
    if (idx < 0 || idx >= static_cast<int>(inv.items.size()))
        return nullptr;
    return &inv.items[static_cast<std::size_t>(idx)];
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
constexpr EquipSlot kAllSlots[] = {
    EquipSlot::RightHand, EquipSlot::LeftHand, EquipSlot::Head,       EquipSlot::Chest,
    EquipSlot::Legs,      EquipSlot::Feet,     EquipSlot::Accessory1, EquipSlot::Accessory2,
};
} // namespace

bool isEquipped(const Equipment& equip, int inv_index)
{
    if (inv_index < 0)
        return false;
    for (const auto s : kAllSlots)
    {
        if (slotIndexConst(equip, s) == inv_index)
            return true;
    }
    return false;
}

EquipSlot equippedInSlot(const Equipment& equip, int inv_index)
{
    for (const auto s : kAllSlots)
    {
        if (slotIndexConst(equip, s) == inv_index)
            return s;
    }
    return EquipSlot::RightHand;
}

// Reindex the equipment slot indices after `removed` has been erased from
// the inventory: any slot pointing at `removed` becomes -1; any slot
// pointing past `removed` shifts down by one.
static void adjustIndicesAfterRemoval(Equipment& equip, int removed)
{
    for (const auto s : kAllSlots)
    {
        int& idx = slotIndex(equip, s);
        if (idx == removed)
            idx = -1;
        else if (idx > removed)
            --idx;
    }
}

bool addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry)
{
    const ItemDef* def = registry.find(item.config_path);
    const bool stackable = (def != nullptr) && def->stackable;
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
    // If this index is already equipped in another slot, clear that slot
    // before assigning to the target.
    for (const auto s : kAllSlots)
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
        const std::size_t ui = static_cast<std::size_t>(i);
        if (inv.items[ui].config_path != config_path)
            continue;
        if (inv.items[ui].quantity <= remaining)
        {
            remaining -= inv.items[ui].quantity;
            inv.items.erase(inv.items.begin() + i);
            adjustIndicesAfterRemoval(equip, i);
        }
        else
        {
            inv.items[ui].quantity -= remaining;
            remaining = 0;
        }
    }
    return remaining <= 0;
}

} // namespace selva::InventoryOps
