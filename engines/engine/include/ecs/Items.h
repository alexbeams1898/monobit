#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Item / inventory / equipment / crafting schemas. Pure data, no logic.
// Ported from games/prison-escape-game/ as engine-layer reusable across
// any game in the workspace. Game-specific item content (the JSON files
// that populate ItemRegistry, RecipeRegistry, etc.) lives in each game.

namespace engine::ecs
{

enum class Rarity : std::uint8_t
{
    VeryCommon = 0,
    Common,
    Uncommon,
    Rare,
    Epic,
    Legendary
};

enum class QualityTier : std::uint8_t
{
    Crude = 0,
    Common,
    Fine,
    Superior,
    Masterwork
};

enum class ItemCategory : std::uint8_t
{
    Weapon,
    Armor,
    Consumable,
    KeyItem,
    Material,
    Money,
    Accessory
};

enum class ArmorSlot : std::uint8_t
{
    Head,
    Chest,
    Legs,
    Feet
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
    Accessory2
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

// Item template / blueprint. Read-only after loading. Lookup keyed on
// config_path (the JSON file the template was loaded from).
struct ItemDef
{
    std::string config_path;
    std::string name;
    std::string description;
    std::string icon_path;
    ItemCategory category = ItemCategory::Material;
    Rarity rarity = Rarity::Common;

    // Weapon fields (only meaningful when category == Weapon).
    float base_damage = 0.0f;
    float weight = 0.5f;
    float str_scaling = 0.0f;
    float dex_scaling = 0.0f;
    int str_requirement = 0;
    int dex_requirement = 0;
    std::string weapon_tier;         // references WeaponTierRegistry
    float damage_per_level = -1.0f;  // -1 = use tier default
    float scaling_per_level = -1.0f; // -1 = use tier default

    // Ranged weapon fields (only meaningful when category == Weapon && ranged).
    bool ranged = false;
    float projectile_speed = 400.0f;
    float effective_range = 500.0f;
    int magazine_size = 0;
    float reload_time = 1.5f;
    float spread = 0.0f;
    int projectile_count = 1;
    float projectile_size = 6.0f;
    int pierce = 0;
    std::string projectile_sprite;
    std::string ammo_type;
    std::string fire_sound;
    float fire_rate = 0.0f;
    float stamina_cost = -1.0f;

    // Visual weapon fields. 2D-specific (sprite grip points) but kept on
    // the engine ItemDef so a port doesn't need a per-game schema split.
    // 3D games ignore the grip_* / weapon_icon fields; they're harmless.
    std::string visual_weapon;
    std::string weapon_icon;
    float grip_x = 0.0f;
    float grip_y = 0.0f;
    std::string attack_icon_ns;
    float attack_grip_ns_x = 0.0f;
    float attack_grip_ns_y = 0.0f;
    float attack_fore_grip_ns_x = 0.0f;
    float attack_fore_grip_ns_y = 0.0f;
    float fore_grip_x = 0.0f;
    float fore_grip_y = 0.0f;
    float weapon_scale = 1.0f;
    float base_rotation = 0.0f;
    bool two_handed = false;
    std::string attack_anim;
    std::vector<int> shoot_frames;

    // Armor fields (only meaningful when category == Armor).
    ArmorSlot armor_slot = ArmorSlot::Chest;
    float defense_bonus = 0.0f;
    float poise_bonus = 0.0f;

    // Shield (max_guard > 0 means this item acts as a shield).
    float max_guard = 0.0f;
    float parry_window = 0.15f;

    // Accessory stat boosts.
    int str_bonus = 0;
    int dex_bonus = 0;
    int end_bonus = 0;
    int lck_bonus = 0;

    float max_durability = 100.0f;
    bool stackable = false;
    int max_stack = 1;
    int value = 0; // currency denomination (Money category)
};

struct ItemRegistry
{
    std::unordered_map<std::string, ItemDef> defs;
    bool loaded = false;

    const ItemDef* find(const std::string& path) const
    {
        const auto it = defs.find(path);
        return (it != defs.end()) ? &it->second : nullptr;
    }
};

// Per-copy instance state. Template data lives in ItemDef (via config_path).
struct ItemInstance
{
    std::string config_path;
    QualityTier quality = QualityTier::Common;
    float durability = 100.0f;
    int quantity = 1;
    float evolution_bonus = 0.0f;
    bool newly_discovered = false;
    int weapon_xp_level = 1;
    float weapon_xp_current = 0.0f;

    bool empty() const
    {
        return config_path.empty();
    }
};

// Bag of items. Index-addressed; Equipment refers to slot positions here.
struct Inventory
{
    std::vector<ItemInstance> items;
    int max_slots = 20;
};

// Currently equipped items. Each slot is an index into Inventory::items.
// -1 = nothing equipped. Items stay in inventory; equipping just marks
// which index each slot uses. Game-side systems sync these to render /
// combat / stat-stacking each frame.
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

// A world-space pickup entity. Carries xp_value (used by pickup systems)
// + a single ItemInstance (the dropped item).
struct Pickup
{
    int xp_value = 0;
    float radius = 48.0f;
    ItemInstance item;
};

// One possible material drop from a dying entity.
struct DropEntry
{
    std::string config_path;
    int min_qty = 1;
    int max_qty = 1;
    float base_chance = 1.0f;
};

// Loot table attached to an enemy entity.
struct Loot
{
    int xp_drop = 20;
    int level = 1;
    std::vector<DropEntry> drops;
};

// Crafting recipe definition.
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

// Per-weapon-class default growth values. Looked up by ItemDef::weapon_tier.
struct WeaponTierDef
{
    float damage_per_level = 1.0f;
    float scaling_per_level = 0.02f;
    float xp_rate = 1.0f;
};

struct WeaponTierRegistry
{
    std::unordered_map<std::string, WeaponTierDef> tiers;
    bool loaded = false;

    const WeaponTierDef* find(const std::string& tier) const
    {
        const auto it = tiers.find(tier);
        return (it != tiers.end()) ? &it->second : nullptr;
    }
};

// Weapon evolution tree.
struct EvolutionPath
{
    std::string target_node;
    int min_level = 1;
    std::string material_config_path; // empty = flat upgrade
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

// Discovered-items log. Persists across runs (meta-progression).
struct Compendium
{
    std::unordered_set<std::string> discovered;

    bool isDiscovered(const std::string& config_path) const
    {
        return discovered.count(config_path) > 0;
    }
    bool discover(const std::string& config_path)
    {
        return discovered.insert(config_path).second;
    }
};

} // namespace engine::ecs
