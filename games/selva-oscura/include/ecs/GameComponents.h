#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Selva-side ECS components for inventory and equipment.
//
// Ported from games/prison-escape-game/include/ecs/GameComponents.h. The
// same data model (slot-indexed Equipment + Inventory with ItemInstance
// vector) is reused; ranged-weapon-specific fields are dropped since Selva
// is melee-only. Crafting / recipes / weapon-XP / pickup-radius fields are
// dropped since those systems aren't part of Selva's v1 scope.
//
// This file is intentionally small. As Selva's inventory needs grow, port
// more fields from prison-escape on an as-needed basis rather than copying
// the whole graph at once.
// ---------------------------------------------------------------------------

namespace selva
{

enum class ItemCategory : std::uint8_t
{
    Weapon,
    Armor,
    Consumable,
    KeyItem,
    Material,
    Accessory,
};

enum class QualityTier : std::uint8_t
{
    Crude = 0,
    Common,
    Fine,
    Superior,
    Masterwork,
};

enum class ArmorSlot : std::uint8_t
{
    Head,
    Chest,
    Legs,
    Feet,
};

enum class EquipSlot : std::uint8_t
{
    RightHand,
    LeftHand,
    Head,
    Chest,
    Legs,
    Feet,
    Accessory1,
    Accessory2,
};

// One concrete item instance. Template data lives in ItemDef (looked up via
// config_path from ItemRegistry). Instance data is per-copy.
struct ItemInstance
{
    std::string config_path;
    QualityTier quality = QualityTier::Common;
    float durability = 100.0f;
    int quantity = 1; // >1 only for stackable items

    bool empty() const
    {
        return config_path.empty();
    }
};

// Inventory - the bag of items the entity carries.
struct Inventory
{
    std::vector<ItemInstance> items;
    int max_slots = 20;
};

// Equipment - currently equipped items by slot. Each value is an index into
// Inventory::items, or -1 for nothing equipped. Items remain in inventory
// when equipped; equipping just marks which slot uses which inventory index.
struct Equipment
{
    int right_hand = -1;
    int left_hand = -1;
    int head = -1;
    int chest = -1;
    int legs = -1;
    int feet = -1;
    int accessory_1 = -1;
    int accessory_2 = -1;
};

} // namespace selva
