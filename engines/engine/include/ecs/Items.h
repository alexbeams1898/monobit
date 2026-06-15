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
    Accessory,
    Incantation,
    Invocation
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

    // Weapon fields (only meaningful when category == Weapon). All
    // SEVEN universal stat-scaling/requirement axes live here -- the
    // body four (STR/DEX/END/LCK) and the mind three (PER/COG/INT)
    // -- so any weapon can scale on any combination. A mind-coded
    // staff scales on INT + COG; a body-coded mace on STR + END;
    // bizarre cross-coded weapons (DEX + COG) are equally legal at
    // the data layer. Identity-stat scaling (per-game cosmology) is
    // a separate concern; in Selva it lives in selva::items::
    // ItemExtensions. Default 0 means "this axis doesn't
    // contribute," so weapons authored against the legacy STR/DEX-
    // only model continue to behave identically.
    float base_damage = 0.0f;
    float weight = 0.5f;
    float str_scaling = 0.0f;
    float dex_scaling = 0.0f;
    float end_scaling = 0.0f;
    float lck_scaling = 0.0f;
    float per_scaling = 0.0f;
    float cog_scaling = 0.0f;
    float int_scaling = 0.0f;
    int str_requirement = 0;
    int dex_requirement = 0;
    int end_requirement = 0;
    int lck_requirement = 0;
    int per_requirement = 0;
    int cog_requirement = 0;
    int int_requirement = 0;
    std::string weapon_tier;         // references WeaponTierRegistry
    float damage_per_level = -1.0f;  // -1 = use tier default
    float scaling_per_level = -1.0f; // -1 = use tier default
    // Animset key. Games that drive swing animations from a
    // class-based WeaponClass registry resolve the live animation
    // set from this id. Empty = unarmed / no class.
    std::string weapon_class_id;

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

    // Path to a static mesh (.glb / .gltf) drawn at this item's
    // pickup position when it sits in the world (as a dropped pickup
    // or a procedurally-spawned gather node). Empty = no mesh; the
    // pickup falls back to the per-game glow-sprite rendering.
    // Materials and small props use this; weapons typically keep
    // their glow rendering instead so the player can spot them at
    // distance. Path is resolved by the per-game pickup-mesh render
    // pass against the static-mesh loader.
    std::string world_mesh;
    // Visual weapon fields. 2D-specific (sprite grip points) but kept on
    // the engine ItemDef so a port doesn't need a per-game schema split.
    // 3D games ignore the grip_* / weapon_icon fields; they're harmless.
    std::string visual_weapon;
    std::string weapon_icon;
    float grip_x = 0.0f;
    float grip_y = 0.0f;
    // 3D-specific grip pose. The 4x4 grip transform applied AFTER the
    // hand-bone world matrix when drawing the equipped weapon mesh:
    //   final_model = hand_world * T(grip_offset) * R(grip_rot_euler) * S(grip_scale)
    // Translation slides the mesh so the GRIP POINT (where the
    // character's fingers wrap the handle) lands at the wrist joint
    // instead of the mesh origin. Rotation orients the haft along the
    // hand's forward axis. Scale is a uniform multiplier (most weapons
    // 1.0; placeholder meshes may need rescaling). All zero / scale=1
    // means "use the mesh's authored pose at the hand joint" -- which
    // is almost always wrong, but it's a defensible default that
    // doesn't hide bugs.
    //
    // Euler degrees in XYZ order (pitch / yaw / roll). 2D games ignore.
    float grip_offset_x = 0.0f;
    float grip_offset_y = 0.0f;
    float grip_offset_z = 0.0f;
    float grip_rot_deg_x = 0.0f;
    float grip_rot_deg_y = 0.0f;
    float grip_rot_deg_z = 0.0f;
    float grip_scale = 1.0f;
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

using ItemInstanceId = std::uint64_t;
constexpr ItemInstanceId kInvalidItemInstanceId = 0;

// Per-copy instance state. Template data lives in ItemDef (via config_path).
// `id` is stable across all inventory mutations -- Equipment slots address
// items by id, never by vector index. `id` is assigned by Inventory::add /
// loaded from save; do not assign by hand.
struct ItemInstance
{
    ItemInstanceId id = kInvalidItemInstanceId;
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

// Bag of items, category-bucketed. The bucket key is the item's category
// (Weapons/Armor/Consumables/Materials/KeyItems/Accessories/Incants),
// resolved from ItemDef.category at add time. Total capacity is unbounded;
// per-ItemDef carry caps + overflow routing live in game-side ops.
//
// `next_id` is the monotonic id allocator. NEVER reused on remove --
// equipment slots and any other cross-system handle stay valid for the
// lifetime of the character. Persists across save/load.
struct Inventory
{
    std::unordered_map<std::string, std::vector<ItemInstance>> by_category;
    ItemInstanceId next_id = 1;
};

// Currently equipped items. Each slot stores the equipped item's stable id.
// kInvalidItemInstanceId = nothing equipped. Items stay in inventory;
// equipping just marks which id each slot uses. Game-side systems sync
// these to render / combat / stat-stacking each frame.
struct Equipment
{
    ItemInstanceId right_hand = kInvalidItemInstanceId;
    ItemInstanceId left_hand = kInvalidItemInstanceId;
    ItemInstanceId head = kInvalidItemInstanceId;
    ItemInstanceId chest = kInvalidItemInstanceId;
    ItemInstanceId legs = kInvalidItemInstanceId;
    ItemInstanceId feet = kInvalidItemInstanceId;
    ItemInstanceId accessory_1 = kInvalidItemInstanceId;
    ItemInstanceId accessory_2 = kInvalidItemInstanceId;
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

// Weighted-selection pool: pick ONE entry from this distribution.
// Sibling to Loot/DropEntry which models INDEPENDENT probabilities
// for multi-drop events (enemy kills, container opens). WeightedPool
// models pick-one-from-distribution events (gather node spawn type,
// random encounter selection, future skill-mod rolls).
//
// Weights are relative integers; the roll helper normalizes by sum,
// so authors can edit one entry's weight without re-balancing the
// others. Empty pool returns empty config_path on roll.
struct WeightedEntry
{
    std::string config_path;
    int weight = 1;
};

struct WeightedPool
{
    std::vector<WeightedEntry> entries;
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
    // Cost the crafting verb deducts from a per-game cost pool. Default
    // 0 = free. Engine treats this as an opaque integer; the game-side
    // craft call is responsible for checking + deducting.
    int sangue_cost = 0;
    // Substrate tag drives per-game persistence rules. Empty = no tag.
    // Selva uses "hell" / "wood" to decide whether outputs reclaim on
    // second death; other games may ignore.
    std::string substrate;
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
