#pragma once

#include <entt/entt.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Game components -- specific to this game's rules and mechanics.
// Engine components live in engine/include/ecs/Components.h.
//
// Hard rule: nothing here may be included from engine/ headers. Game systems
// include both this file and Components.h. Engine systems include only
// Components.h and must never reference anything defined here.
// ---------------------------------------------------------------------------

// PlayerActions -- all player input state: movement, combat actions, ability triggers.
// InputMappingSystem reads raw SDL keyboard state and writes these fields each frame.
struct PlayerActions
{
    // Movement intent from WASD (normalized directional vector).
    float move_x = 0.0f;
    float move_y = 0.0f;

    bool attack = false;
    bool dodge = false;
    bool skill = false;
    bool sprint = false;
    bool block_held = false;
    bool block_just_pressed = false;
    bool auto_toggle_just_pressed = false;
    bool start_wave = false;
    bool craft = false;
    bool cycle_weapon = false;
    bool interact = false;
    bool mouse_click = false;
    float mouse_world_x = 0.0f;
    float mouse_world_y = 0.0f;
    bool alloc_str = false;
    bool alloc_dex = false;
    bool alloc_end = false;
    bool alloc_lck = false;
    float dodge_cooldown_remaining = 0.0f;
    float step_timer = 0.0f;
    float wall_bump_cooldown = 0.0f;
};

// ---------------------------------------------------------------------------
// Combat transient components
// ---------------------------------------------------------------------------

// Hitbox -- a one-frame entity spawned by CombatSystem on each attack swing.
struct Hitbox
{
    float damage = 0.0f;
    entt::entity owner = entt::null;
};

// Dodging -- active while the player is in a dodge roll. Grants i-frames.
struct Dodging
{
    float remaining = 0.0f;
};

// AttackLocked -- animation commitment window after a swing.
struct AttackLocked
{
    float remaining = 0.0f;
};

// Staggered -- guard-break or parry result; entity cannot act until expired.
struct Staggered
{
    float remaining = 0.0f;
};

// DamageFeedback -- emplaced by DamageSystem when an entity takes a hit.
// TintSystem reads this to apply the white flash via TintOverride.
struct DamageFeedback
{
    float remaining = 0.0f;
};

// AttackFeedback -- emplaced by CombatSystem when an entity swings.
struct AttackFeedback
{
    float remaining = 0.0f;
};

// ---------------------------------------------------------------------------
// RPG / progression components
// ---------------------------------------------------------------------------

// Stats -- base stats for any entity. Levelable by the player.
struct Stats
{
    int str = 1;
    int dex = 1;
    int end = 1;
    int lck = 1;
};

// Body -- physical material properties of a creature. Not levelable.
// Represents what the creature IS made of (bone, flesh, demon hide, etc.).
// See DESIGN.md "HP vs DEF — Design Philosophy".
// Includes natural weapon stats (unarmed attack) -- a skeleton's bony fist
// hits differently than a human's. Defaults match FormulaConfig::fist
// (tuned for the player). Most enemies should have weaker unarmed scaling
// so that a trained, unarmed player still feels dangerous.
struct Body
{
    int base_hp = 0;
    int base_defense = 0;

    // Natural weapon (unarmed attack derived from body composition).
    float unarmed_damage = 5.0f;
    float unarmed_weight = 0.5f;
    float unarmed_str_scaling = 1.0f;
    float unarmed_dex_scaling = 0.75f;
};

// Experience -- tracks level progression and unspent stat allocation points.
struct Experience
{
    int current_xp = 0;
    int xp_to_next = 100;
    int level = 1;
    int stat_points = 0;
};

// Weapon -- equipped weapon state and runtime cooldown timers.
struct Weapon
{
    std::string name;
    float weight = 0.5f;
    float str_scaling = 0.25f;
    float dex_scaling = 0.25f;
    int str_requirement = 0;
    int dex_requirement = 0;
    float base_damage = 5.0f;

    float swing_cooldown_remaining = 0.0f;
    float skill_cooldown_remaining = 0.0f;
};

// Shield -- one-handed shield equipped in the off-hand slot.
struct Shield
{
    float guard_health = 100.0f;
    float max_guard = 100.0f;
    bool blocking = false;
};

// Parrying -- brief invulnerability + stagger window opened by a timed block.
struct Parrying
{
    float remaining = 0.0f;
};

// Poise -- determines how many hits an entity can absorb before staggering.
struct Poise
{
    float max = 0.0f;
    float current = 0.0f;
    float decay_timer = 0.0f;
};

// DropEntry -- one possible material drop from a dying entity.
struct DropEntry
{
    std::string config_path;
    int min_qty = 1;
    int max_qty = 1;
    float base_chance = 1.0f;
};

// Loot -- reward data dropped when an entity dies.
struct Loot
{
    int xp_drop = 20;
    int level = 1;
    std::vector<DropEntry> drops;
};

// RestSpot -- a static entity the player can stand on to restore HP.
struct RestSpot
{
    float radius = 64.0f;
    float cooldown = 0.0f;
};

// AutoAttackMode -- when enabled, CombatSystem fires weapons automatically.
struct AutoAttackMode
{
    bool enabled = false;
};

// AIController -- drives non-player entity behavior.
struct AIController
{
    enum class State
    {
        Idle,
        Chase,
        Attack
    };

    State state = State::Idle;
    float turn_speed = 8.0f;
    float aggro_radius = 0.0f;
    float separation_strength = 1.0f;
    float arrival_radius = 0.0f;
    float attack_radius = 0.0f;
    int tier = 1;
    float sprint_multiplier = 0.0f;
    float sprint_threshold = 0.0f;
    bool sprint = false;
};

// Essence -- per-stat natural talent (0-100 scale).
struct Essence
{
    int str = 0;
    int dex = 0;
    int end = 0;
    int lck = 0;
};

// Stamina -- real resource pool depleted by combat actions and sprint.
struct Stamina
{
    float current = 0.0f;
    float max_stamina = 0.0f;
    float recovery_timer = 0.0f;
};

// Marks an entity as part of the active wave for wave-clear detection.
struct WaveEnemy
{
};

// ---------------------------------------------------------------------------
// Item / inventory / equipment components
// ---------------------------------------------------------------------------

enum class ItemCategory : uint8_t
{
    Weapon,
    Armor,
    Consumable,
    KeyItem,
    Material,
    Money
};

enum class QualityTier : uint8_t
{
    Crude = 0,
    Common,
    Fine,
    Superior,
    Masterwork
};

enum class ArmorSlot : uint8_t
{
    Head,
    Chest,
    Legs,
    Feet
};

enum class EquipSlot : uint8_t
{
    MainHand,
    OffHand,
    Head,
    Chest,
    Legs,
    Feet,
    Accessory1,
    Accessory2
};

// One concrete item instance. Template data lives in ItemDef (looked up via
// config_path from ItemRegistry). Instance data is per-copy.
struct ItemInstance
{
    std::string config_path;
    QualityTier quality = QualityTier::Common;
    float durability = 100.0f;
    int upgrade_level = 0;
    int quantity = 1; // >1 only for stackable items
    bool empty() const
    {
        return config_path.empty();
    }
};

// Pickup -- a collectible entity left by dead enemies (XP or item).
struct Pickup
{
    int xp_value = 0;
    float radius = 48.0f;
    ItemInstance item;
};

// Bag of items the entity carries.
struct Inventory
{
    std::vector<ItemInstance> items;
    int max_slots = 20;
};

// Currently equipped items. Each slot holds a copy of the ItemInstance.
// Empty slot = config_path.empty(). EquipmentSystem syncs these to
// Weapon/Shield components each frame.
struct Equipment
{
    ItemInstance main_hand;
    ItemInstance off_hand;
    ItemInstance head;
    ItemInstance chest;
    ItemInstance legs;
    ItemInstance feet;
    ItemInstance accessory_1;
    ItemInstance accessory_2;
    bool two_handing = false;

    // Index into Inventory::items for the currently equipped weapon.
    // -1 = fists (no inventory slot). Used by Tab cycling to avoid ambiguity
    // when multiple weapons share the same config_path + quality.
    int main_hand_slot = -1;

    // EquipmentSystem compares these to detect slot changes.
    std::string synced_main_hand;
    std::string synced_off_hand;
};

// Wallet -- persistent money balance for the player.
struct Wallet
{
    int money = 0;
};

// InteractTarget -- set by PickupSystem each frame to indicate the pickup
// the player is currently targeting (mouse hover or proximity).
struct InteractTarget
{
    entt::entity entity = entt::null;
};
