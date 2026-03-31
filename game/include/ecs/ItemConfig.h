#pragma once

#include "GameComponents.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// Item definitions, registries, crafting recipes, weapon tiers, evolution
// trees, and the discovery compendium.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// ItemDef -- item template/blueprint. Read-only after loading.
// ---------------------------------------------------------------------------

enum class Rarity : uint8_t
{
    VeryCommon = 0,
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary
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

struct ItemDef
{
    std::string config_path;
    std::string name;
    std::string description;
    std::string icon_path;
    ItemCategory category = ItemCategory::Material;
    Rarity rarity = Rarity::Common;

    // Weapon-specific (only meaningful when category == Weapon).
    float base_damage = 0.0f;
    float weight = 0.5f;
    float str_scaling = 0.0f;
    float dex_scaling = 0.0f;
    int str_requirement = 0;
    int dex_requirement = 0;
    bool two_handed = false;
    std::string weapon_tier;         // references WeaponTierRegistry for growth defaults
    float damage_per_level = -1.0f;  // -1 = use tier default
    float scaling_per_level = -1.0f; // -1 = use tier default

    // Armor-specific (only meaningful when category == Armor).
    ArmorSlot armor_slot = ArmorSlot::Chest;
    float defense_bonus = 0.0f;
    float poise_bonus = 0.0f;

    // Shield (armor in off-hand; max_guard > 0 means this is a shield).
    float max_guard = 0.0f;
    float parry_window = 0.15f; // seconds; parsed but used by future parry system

    // Accessory-specific stat boosts.
    int str_bonus = 0;
    int dex_bonus = 0;
    int end_bonus = 0;
    int lck_bonus = 0;

    float max_durability = 100.0f;
    bool stackable = false;
    int max_stack = 1;
    int value = 0; // Money denomination (only meaningful for Money category).
};

// ---------------------------------------------------------------------------
// ItemRegistry -- all loaded item definitions, keyed by config path.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// RecipeRegistry -- crafting recipes loaded from config/recipes/.
// ---------------------------------------------------------------------------
struct RecipeIngredient
{
    std::string config_path;
    int quantity = 1;
};

struct RecipeDef
{
    std::string config_path;
    std::string name;
    std::vector<RecipeIngredient> inputs;
    std::string output_item;
    int output_quantity = 1;
};

struct RecipeRegistry
{
    std::vector<RecipeDef> recipes;
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// WeaponTierRegistry -- default per-level growth values per weapon class.
// Loaded from config/balance/weapon_tiers.json.
// ---------------------------------------------------------------------------
struct WeaponTierDef
{
    float damage_per_level = 1.0f;
    float scaling_per_level = 0.02f;
    float xp_rate = 1.0f; // multiplier on XP thresholds (< 1 = faster leveling)
};

struct WeaponTierRegistry
{
    std::unordered_map<std::string, WeaponTierDef> tiers;
    bool loaded = false;

    const WeaponTierDef* find(const std::string& tier) const
    {
        auto it = tiers.find(tier);
        return (it != tiers.end()) ? &it->second : nullptr;
    }
};

// ---------------------------------------------------------------------------
// EvolutionRegistry -- weapon evolution tree data.
// Loaded from config/evolution/*.json (one file per weapon class).
// ---------------------------------------------------------------------------
struct EvolutionPath
{
    std::string target_node;
    int min_level = 1;
    std::string material_config_path; // empty = flat upgrade (no material)
    int material_qty = 1;
};

struct EvolutionNode
{
    std::string weapon_config_path;
    std::vector<EvolutionPath> evolutions;
};

struct EvolutionFamily
{
    std::string name;
    std::unordered_map<std::string, EvolutionNode> nodes;
};

struct EvolutionRegistry
{
    // weapon config_path -> (family index, node id)
    std::unordered_map<std::string, std::pair<int, std::string>> weapon_to_node;
    std::vector<EvolutionFamily> families;
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// Compendium -- persistent set of discovered item config_paths.
// Persists across runs (meta-progression).
// ---------------------------------------------------------------------------
struct Compendium
{
    std::unordered_set<std::string> discovered;

    bool isDiscovered(const std::string& config_path) const
    {
        return discovered.count(config_path) > 0;
    }
    bool discover(const std::string& config_path)
    {
        return discovered.insert(config_path).second; // true if newly inserted
    }
};
