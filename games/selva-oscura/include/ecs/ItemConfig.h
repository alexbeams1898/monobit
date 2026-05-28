#pragma once

#include "ecs/GameComponents.h"

#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Selva-side item definitions and registry.
//
// Ported from games/prison-escape-game/include/ecs/ItemConfig.h, stripped
// to the fields Selva actually uses. Removed: all ranged-weapon fields
// (projectile_speed, ammo_type, fire_rate, magazine_size, spread, etc.),
// weapon-tier / evolution / compendium / recipe systems, paper-doll grip
// pixel fields. Kept: the core ItemDef -> ItemRegistry pattern with a
// config_path-keyed lookup.
//
// JSON loader is not yet implemented in v1. ItemRegistry is empty at
// startup; items will be added when the Hell-side content needs them.
// ---------------------------------------------------------------------------

namespace selva
{

enum class Rarity : std::uint8_t
{
    VeryCommon = 0,
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary,
};

inline const char* rarityName(Rarity r)
{
    switch (r)
    {
    case Rarity::VeryCommon:
        return "Very Common";
    case Rarity::Common:
        return "Common";
    case Rarity::Uncommon:
        return "Uncommon";
    case Rarity::Rare:
        return "Rare";
    case Rarity::Epic:
        return "Epic";
    case Rarity::Legendary:
        return "Legendary";
    }
    return "Common";
}

inline const char* qualityName(QualityTier q)
{
    switch (q)
    {
    case QualityTier::Crude:
        return "Crude";
    case QualityTier::Common:
        return "Common";
    case QualityTier::Fine:
        return "Fine";
    case QualityTier::Superior:
        return "Superior";
    case QualityTier::Masterwork:
        return "Masterwork";
    }
    return "Common";
}

// ItemDef - item template/blueprint. Loaded from config JSON, then
// read-only for the rest of the run.
struct ItemDef
{
    std::string config_path;
    std::string name;
    std::string description;
    std::string icon_path;
    ItemCategory category = ItemCategory::Material;
    Rarity rarity = Rarity::Common;

    // Weapon-specific (only meaningful when category == Weapon). Selva is
    // melee-only - ranged weapon fields from prison-escape are not ported.
    float base_damage = 0.0f;
    float weight = 0.5f;
    float str_scaling = 0.0f;
    float dex_scaling = 0.0f;
    int str_requirement = 0;
    int dex_requirement = 0;
    bool two_handed = false;

    // Armor-specific (only meaningful when category == Armor).
    ArmorSlot armor_slot = ArmorSlot::Chest;
    float defense_bonus = 0.0f;
    float poise_bonus = 0.0f;

    // Shield-specific. max_guard > 0 means this item acts as a shield in
    // whichever hand holds it.
    float max_guard = 0.0f;
    float parry_window = 0.15f;

    float max_durability = 100.0f;
    bool stackable = false;
    int max_stack = 1;
};

// ItemRegistry - all loaded item definitions, keyed by config_path. Empty
// at v1 startup; populated by loader when item authoring begins.
struct ItemRegistry
{
    std::unordered_map<std::string, ItemDef> defs;
    bool loaded = false;

    const ItemDef* find(const std::string& path) const
    {
        auto it = defs.find(path);
        return (it != defs.end()) ? &it->second : nullptr;
    }
};

// Process-wide singleton accessor for the item registry (same pattern as
// selva::anim::clips() / selva::gameplay::archetypes()). Mutable so the
// loader can populate it; treat as read-only after startup.
ItemRegistry& itemRegistry();

} // namespace selva
