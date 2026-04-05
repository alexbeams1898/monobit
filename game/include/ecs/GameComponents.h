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
    bool lock_on_toggle = false;
    bool reload = false;
    bool toggle_inventory = false;
    bool toggle_pause = false;
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
    bool hit_something = false;
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

// LockOnTarget -- souls-style focus on a single enemy.
struct LockOnTarget
{
    entt::entity target = entt::null;
};

// RiposteWindow -- after a successful parry, next attack within this window is a critical.
struct RiposteWindow
{
    float remaining = 0.0f;
};

// CriticalAttacking -- attacker frozen during critical animation (backstab/riposte).
struct CriticalAttacking
{
    float remaining = 0.0f;
    entt::entity target = entt::null;
};

// CriticalTarget -- target frozen and invulnerable during critical animation.
struct CriticalTarget
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

    // Unarmed fighting XP (persists across weapon switches).
    int unarmed_xp_level = 1;
    float unarmed_xp_current = 0.0f;
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

    // Ranged weapon fields (set by EquipmentSystem from ItemDef).
    bool ranged = false;
    float projectile_speed = 400.0f;
    float effective_range = 500.0f;
    float spread = 0.0f;           // accuracy cone in degrees (0 = perfect)
    int projectile_count = 1;      // per shot (>1 for shotgun-type weapons)
    float projectile_size = 6.0f;  // collider width/height
    int pierce = 0;                // enemies a projectile passes through
    std::string projectile_sprite; // empty = fallback to colored square
    std::string ammo_type;         // config_path of ammo item consumed per shot
    std::string fire_sound;        // sound event key (e.g. "gunshot", "bow_release")
    float fire_rate = 0.0f;        // shots/sec; >0 overrides swing cooldown formula
    float stamina_cost = -1.0f;    // per-attack cost; <0 = use weight-based formula
};

// WeaponXP -- tracks weapon leveling through combat use.
// Attached to entities whose equipped weapon is earning XP.
struct WeaponXP
{
    int level = 1;
    float current_xp = 0.0f;
    float xp_to_next = 50.0f;
};

// Shield -- one-handed shield equipped in the off-hand slot.
struct Shield
{
    float guard_health = 100.0f;
    float max_guard = 100.0f;
    bool blocking = false;
};

// ArmorStats -- aggregated defensive stats from all equipped armor pieces.
// Computed by EquipmentSystem each time equipment changes.
struct ArmorStats
{
    float total_defense = 0.0f;
    float total_poise_bonus = 0.0f;
    float total_weight = 0.0f;
    float equip_load_ratio = 0.0f;
    int load_tier = 0; // 0=light, 1=medium, 2=heavy, 3=overloaded
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
    float deaggro_radius = 0.0f; // Chase -> Idle leash (0 = never deaggro)
    float separation_strength = 1.0f;
    float arrival_radius = 0.0f;
    float attack_radius = 0.0f;
    float speed_multiplier = 1.0f;
    int tier = 1;
    float sprint_multiplier = 0.0f;
    float sprint_threshold = 0.0f;
    float orbit_speed = 0.5f;     // slot rotation speed multiplier (0=stationary, 1=base rate)
    float attack_cooldown = 0.0f; // minimum seconds between attacks (overrides weapon swing speed)

    // Assigned angular position around the player for attack positioning.
    // NO_SLOT sentinel is outside atan2's [-pi, pi] range so negative angles
    // (enemy north of player) don't collide with the "unassigned" check.
    static constexpr float NO_SLOT = -100.0f;
    float slot_angle = NO_SLOT;
    bool sprint = false;
    float token_cooldown = 0.0f; // time until entity can claim an attack token
    int stuck_ticks = 0;         // consecutive ticks with near-zero velocity (debug)
};

// Limits concurrent enemy attackers. Stored in entt::registry::ctx().
// Holders vector is at most max_tokens elements (default 2).
struct AttackTokenPool
{
    int max_tokens = 2;
    std::vector<entt::entity> holders;
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
    Money,
    Accessory
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
    int quantity = 1;              // >1 only for stackable items
    float evolution_bonus = 0.0f;  // carry-forward stat bonus from prior evolution
    bool newly_discovered = false; // first-time pickup; UI shows "!" badge
    int weapon_xp_level = 1;
    float weapon_xp_current = 0.0f;
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
    // Sentinel ensures the first update always triggers sync (even for fists).
    std::string synced_main_hand = "__unsynced__";
    std::string synced_off_hand = "__unsynced__";
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

// HitSound -- per-entity sound played when the entity takes damage.
// Overrides the global SoundConfig::hit for this entity.
struct HitSound
{
    std::string path;
    float volume = 0.5f;
    float min_pitch = 0.9f;
    float max_pitch = 1.1f;
};

// Ladder -- spawns after a wave clears. Player walks to it and interacts to descend.
struct Ladder
{
    float radius = 48.0f;
    float spawn_timer = 0.0f;
    float spawn_duration = 0.5f;
    bool spawning = true;
};

// AmbientSound -- plays random sounds from a pool at random intervals.
// Mode enum allows future playback algorithms without structural changes.
struct AmbientSound
{
    enum class Mode
    {
        RandomInterval
    };

    std::vector<std::string> paths;
    float volume = 0.3f;
    float min_interval = 3.0f;
    float max_interval = 8.0f;
    float max_distance = 400.0f;
    float min_pitch = 0.7f;
    float max_pitch = 0.9f;
    Mode mode = Mode::RandomInterval;
    float timer = 0.0f;
    int shuffle_index = 0;
    std::vector<int> shuffle_order;
};

// Projectile -- a moving damage entity (bullet, arrow) spawned by CombatSystem.
// ProjectileSystem manages lifetime, wall destruction, and pierce logic.
struct Projectile
{
    entt::entity owner = entt::null;
    float max_range = 500.0f;
    float spawn_x = 0.0f;
    float spawn_y = 0.0f;
    int pierce_remaining = 0; // 0 = destroy on first hit; >0 = pass through N enemies
    float dir_x = 0.0f;       // normalized flight direction
    float dir_y = 0.0f;
    float speed = 0.0f; // pixels per second
};

// RangedState -- runtime magazine/reload state for a ranged weapon wielder.
struct RangedState
{
    int ammo_in_magazine = 0;
    int magazine_size = 0; // 0 = no magazine (bow-type)
    float reload_timer = 0.0f;
    float reload_time = 1.0f;
    bool reloading = false;
};
