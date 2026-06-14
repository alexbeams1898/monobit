#include "ops/InventoryOps.h"

namespace engine::ops::inventory
{

using engine::ecs::ItemCategory;
using engine::ecs::ItemDef;

namespace
{

constexpr EquipSlot ALL_SLOTS[] = {EquipSlot::RightHand,  EquipSlot::LeftHand,  EquipSlot::Head,
                                   EquipSlot::Chest,      EquipSlot::Legs,      EquipSlot::Feet,
                                   EquipSlot::Accessory1, EquipSlot::Accessory2};

// Stable string keys for category buckets. Inventory.by_category uses
// these as map keys; SaveManager round-trips them verbatim. The plural
// form matches game-side category JSON ids (e.g. selva-oscura's
// `inventory_categories.json`).
const char* categoryKey(ItemCategory c)
{
    switch (c)
    {
    case ItemCategory::Weapon:
        return "weapons";
    case ItemCategory::Armor:
        return "armor";
    case ItemCategory::Consumable:
        return "consumables";
    case ItemCategory::KeyItem:
        return "key_items";
    case ItemCategory::Material:
        return "materials";
    case ItemCategory::Money:
        return "money";
    case ItemCategory::Accessory:
        return "accessories";
    case ItemCategory::Incantation:
        return "incantations";
    case ItemCategory::Invocation:
        return "invocations";
    }
    return "materials";
}

// Clear any slot pointing at `id`. Used on remove.
void clearEquipRefs(Equipment& equip, ItemInstanceId id)
{
    if (id == kInvalidItemInstanceId)
        return;
    for (const auto s : ALL_SLOTS)
    {
        if (slotIdConst(equip, s) == id)
            slotId(equip, s) = kInvalidItemInstanceId;
    }
}

} // namespace

// --- Slot accessors ----------------------------------------------------

ItemInstanceId& slotId(Equipment& equip, EquipSlot slot)
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

ItemInstanceId slotIdConst(const Equipment& equip, EquipSlot slot)
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

// --- Inventory lookup --------------------------------------------------

const ItemInstance* findById(const Inventory& inv, ItemInstanceId id)
{
    if (id == kInvalidItemInstanceId)
        return nullptr;
    for (const auto& [cat, items] : inv.by_category)
    {
        for (const auto& it : items)
        {
            if (it.id == id)
                return &it;
        }
    }
    return nullptr;
}

ItemInstance* findByIdMut(Inventory& inv, ItemInstanceId id)
{
    if (id == kInvalidItemInstanceId)
        return nullptr;
    for (auto& [cat, items] : inv.by_category)
    {
        for (auto& it : items)
        {
            if (it.id == id)
                return &it;
        }
    }
    return nullptr;
}

const ItemInstance* equippedItem(const Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    return findById(inv, slotIdConst(equip, slot));
}

ItemInstance* equippedItemMut(Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    return findByIdMut(inv, slotIdConst(equip, slot));
}

std::string equippedPath(const Inventory& inv, const Equipment& equip, EquipSlot slot)
{
    const auto* it = equippedItem(inv, equip, slot);
    return it != nullptr ? it->config_path : std::string{};
}

// --- Equipment queries -------------------------------------------------

bool slotEmpty(const Equipment& equip, EquipSlot slot)
{
    return slotIdConst(equip, slot) == kInvalidItemInstanceId;
}

bool isEquipped(const Equipment& equip, ItemInstanceId id)
{
    if (id == kInvalidItemInstanceId)
        return false;
    for (const auto s : ALL_SLOTS)
    {
        if (slotIdConst(equip, s) == id)
            return true;
    }
    return false;
}

EquipSlot equippedInSlot(const Equipment& equip, ItemInstanceId id)
{
    for (const auto s : ALL_SLOTS)
    {
        if (slotIdConst(equip, s) == id)
            return s;
    }
    return EquipSlot::RightHand;
}

// --- Add / remove ------------------------------------------------------

ItemInstanceId addItem(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry)
{
    const ItemDef* def = registry.find(item.config_path);
    if (def == nullptr)
        return kInvalidItemInstanceId;

    const std::string bucket = categoryKey(def->category);
    auto& vec = inv.by_category[bucket];

    const bool stackable = def->stackable;
    const int max_stack = def->max_stack;
    int remaining = item.quantity;
    ItemInstanceId last_touched = kInvalidItemInstanceId;

    if (stackable)
    {
        // Merge into existing stacks only when (config_path, quality) match
        // -- quality is part of the item's identity and must survive in the
        // bag so crafting/use can average qualities correctly. Stacks of the
        // same item at different qualities live as separate entries.
        for (auto& existing : vec)
        {
            if (remaining <= 0)
                break;
            if (existing.config_path == item.config_path && existing.quality == item.quality &&
                existing.quantity < max_stack)
            {
                const int space = max_stack - existing.quantity;
                const int to_add = (remaining <= space) ? remaining : space;
                existing.quantity += to_add;
                remaining -= to_add;
                last_touched = existing.id;
            }
        }
        if (remaining <= 0)
            return last_touched;
    }

    ItemInstance remainder = item;
    remainder.quantity = remaining;
    remainder.id = inv.next_id++;
    vec.push_back(remainder);
    return remainder.id;
}

void addWithId(Inventory& inv, const ItemInstance& item, const ItemRegistry& registry)
{
    const ItemDef* def = registry.find(item.config_path);
    // Fall back to "material" bucket if the registry doesn't know this
    // path -- save-load tolerates unknown items rather than dropping
    // them silently.
    const std::string bucket = (def != nullptr) ? categoryKey(def->category) : "material";

    inv.by_category[bucket].push_back(item);

    // Keep next_id strictly greater than any extant id so future
    // allocations don't collide with re-loaded ones.
    if (item.id >= inv.next_id)
        inv.next_id = item.id + 1;
}

bool removeItem(Inventory& inv, Equipment& equip, ItemInstanceId id)
{
    if (id == kInvalidItemInstanceId)
        return false;
    for (auto& [cat, items] : inv.by_category)
    {
        for (auto it = items.begin(); it != items.end(); ++it)
        {
            if (it->id == id)
            {
                items.erase(it);
                clearEquipRefs(equip, id);
                return true;
            }
        }
    }
    return false;
}

// --- Equip -------------------------------------------------------------

bool equipItemToSlot(const Inventory& inv, Equipment& equip, ItemInstanceId id, EquipSlot slot)
{
    if (id == kInvalidItemInstanceId)
        return false;
    if (findById(inv, id) == nullptr)
        return false;

    for (const auto s : ALL_SLOTS)
    {
        if (s != slot && slotIdConst(equip, s) == id)
            slotId(equip, s) = kInvalidItemInstanceId;
    }
    slotId(equip, slot) = id;
    return true;
}

void unequipSlot(Equipment& equip, EquipSlot slot)
{
    slotId(equip, slot) = kInvalidItemInstanceId;
}

// --- Stack ops ---------------------------------------------------------

int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& [cat, items] : inv.by_category)
    {
        for (const auto& it : items)
        {
            if (it.config_path == config_path)
                total += it.quantity;
        }
    }
    return total;
}

bool consumeItems(Inventory& inv, Equipment& equip, const std::string& config_path, int qty)
{
    int remaining = qty;
    for (auto& [cat, items] : inv.by_category)
    {
        for (int i = static_cast<int>(items.size()) - 1; i >= 0 && remaining > 0; --i)
        {
            if (items[i].config_path != config_path)
                continue;

            if (items[i].quantity <= remaining)
            {
                remaining -= items[i].quantity;
                const ItemInstanceId removed_id = items[i].id;
                items.erase(items.begin() + i);
                clearEquipRefs(equip, removed_id);
            }
            else
            {
                items[i].quantity -= remaining;
                remaining = 0;
            }
        }
        if (remaining <= 0)
            break;
    }
    return remaining <= 0;
}

// --- Evolution ---------------------------------------------------------

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
