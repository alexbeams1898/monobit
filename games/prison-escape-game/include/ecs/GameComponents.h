#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// Game components -- specific to this game's rules and mechanics.
// Engine components live in engine/include/ecs/Components.h.
//
// Hard rule: nothing here may be included from engine/ headers. Game systems
// include both this file and Components.h. Engine systems include only
// Components.h and must never reference anything defined here.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Animation state enum and row lookup (game-side).
// The engine Animation component has no concept of named states. This enum
// maps game states to spritesheet row/frames/duration via AnimRowData[].
// AnimStateSystem resolves the current game state, looks up the row config,
// and writes the result into Animation's playback fields each tick.
// ---------------------------------------------------------------------------

enum class AnimState : uint8_t
{
    Idle = 0,
    Walk,
    Attack,
    Hit,
    Death,
    Run,
    COUNT
};

struct AnimRowData
{
    int row = 0;
    int frames = 1;
    float duration = 0.0f;
    bool freeze_on_last = false;
};

// Per-entity lookup table mapping AnimState -> spritesheet row config.
// Emplaced by ConfigLoader alongside the engine Animation component.
struct AnimRowConfig
{
    static constexpr int STATE_COUNT = static_cast<int>(AnimState::COUNT);
    AnimRowData rows[STATE_COUNT]{};
};

// PlayerActions -- all player input state: movement, combat actions, ability triggers.
// InputMappingSystem reads raw SDL keyboard state and writes these fields each frame.
struct PlayerActions
{
    // Movement intent from WASD (normalized directional vector).
    float move_x = 0.0f;
    float move_y = 0.0f;

    bool right_attack = false; // right-hand attack (E / RMB)
    bool left_attack = false;  // left-hand attack (Q / LMB)
    bool dodge = false;
    bool skill = false; // skill attack (Ctrl)
    bool sprint = false;
    bool block_held = false;
    bool block_just_pressed = false;
    bool auto_toggle_just_pressed = false;
    bool cycle_weapon = false;           // right-hand cycle forward (C)
    bool cycle_weapon_prev = false;      // right-hand cycle backward (V)
    bool cycle_left_weapon = false;      // left-hand cycle forward (Z)
    bool cycle_left_weapon_prev = false; // left-hand cycle backward (X)
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
    bool toggle_two_hand = false; // one-shot: flips Weapon.two_handed_active if two_handed
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
    bool left_hand = false;
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
    bool left_hand = false;
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
    bool attacker_left_hand = false;
};

// AttackFeedback -- emplaced by CombatSystem when an entity swings.
struct AttackFeedback
{
    float remaining = 0.0f;
};

// LockOnTarget -- focus held on a single enemy.
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

    // Per-hand unarmed fighting XP (persists across weapon switches).
    int unarmed_xp_level_right = 1;
    float unarmed_xp_current_right = 0.0f;
    int unarmed_xp_level_left = 1;
    float unarmed_xp_current_left = 0.0f;
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

    // Visual weapon fields (set by EquipmentSystem from ItemDef).
    std::string visual_weapon; // weapon id used for equip-change detection
    std::string weapon_icon;   // sprite path for held weapon visual; empty = no visible weapon
    float grip_x = 0.0f;       // primary grip pixel in icon (0..32); trigger hand
    float grip_y = 0.0f;

    // Per-direction attack icons: alternate sprites used during attack animations.
    // If empty, the default weapon_icon is used. Allows top-down perspective for N/S.
    std::string attack_icon_ns;    // icon for N/S attack directions
    float attack_grip_ns_x = 0.0f; // grip (trigger hand) on the NS icon
    float attack_grip_ns_y = 0.0f;
    float attack_fore_grip_ns_x = 0.0f; // fore grip (support hand) on the NS icon
    float attack_fore_grip_ns_y = 0.0f;
    float fore_grip_x = 0.0f; // secondary grip pixel in icon; support hand (two-handed only)
    float fore_grip_y = 0.0f;
    float weapon_scale = 1.0f;  // visual scale (1.0 = native icon size)
    float base_rotation = 0.0f; // resting angle in radians (converted from degrees at load)
    std::string attack_anim;    // animation row name ("slash", "thrust", "shoot"); empty = "slash"
    std::vector<int> shoot_frames; // per-frame column remap for the attack row; empty = play 0..N-1
    entt::entity weapon_entity =
        entt::null; // spawned weapon sprite entity (managed by WeaponSpriteSystem)

    // Two-handed state.
    // two_handed: does the weapon physically support a two-handed grip?
    //                     (also gates the Left-Alt toggle input)
    // two_handed_active:  is the weapon currently being rendered/handled in 2H mode?
    //                     Runtime-only; starts at false on equip and flips on toggle.
    bool two_handed = false;
    bool two_handed_active = false;

    // Per-weapon XP — each hand levels independently.
    int wxp_level = 1;
    float wxp_current = 0.0f;
    float wxp_to_next = 50.0f;
};

// LeftWeapon -- weapon equipped in the left hand. Inherits all Weapon fields;
// separate type so entt can store both Weapon (right hand) and LeftWeapon
// (left hand) on the same entity.
struct LeftWeapon : Weapon
{
};

// WeaponSprite -- tag on the weapon sprite entity linking it back to its wielder.
// `left_hand` indicates whether this sprite represents the left-hand weapon.
struct WeaponSprite
{
    entt::entity wielder = entt::null;
    bool left_hand = false;

    // Smoothed anchor position. Lerped toward the current frame's anchor
    // each render frame to prevent snapping when the animation frame changes.
    float smooth_anchor_x = 0.0f;
    float smooth_anchor_y = 0.0f;
    int prev_row = -1;
    int prev_dir = -1;
};

// WeaponXP -- tracks weapon leveling through combat use.
// Attached to entities whose equipped weapon is earning XP.
struct WeaponXP
{
    int level = 1;
    float current_xp = 0.0f;
    float xp_to_next = 50.0f;
};

// Shield -- equipped in either hand. Blocking is determined by which hand holds it.
struct Shield
{
    float guard_health = 100.0f;
    float max_guard = 100.0f;
    bool blocking = false;
};

// AppearanceState -- cached selections used for the last SpriteCompositor composite.
// AppearanceSyncSystem compares Weapon.visual_weapon against synced_visual_weapon to
// detect equip changes, then re-composites the sprite with updated weapon layers.
struct AppearanceState
{
    std::unordered_map<std::string, std::string> current_selections;
    std::string synced_visual_weapon;
    std::string synced_visual_weapon_left;
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
    // Countdown of the currently-playing rest sound. Drains every frame even
    // when the player is absent so a fresh entry can detect "previous sound
    // still going" and skip retriggering. 0 = nothing playing.
    float sound_timer = 0.0f;
    bool player_present = false;
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
    // Set when stamina hits 0; cleared when stamina recovers to max. While
    // true, sprinting is blocked (walk only) -- forces the player to wait for
    // a full bar before running again. Other stamina actions (attacks, dodges,
    // blocking) are unaffected.
    bool sprint_locked = false;
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
    RightHand,
    LeftHand,
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

// Currently equipped items. Each slot is an index into Inventory::items.
// -1 = nothing equipped (fists for hands, bare for armor). Items stay in
// inventory at all times — equipping just marks which index each slot uses.
// EquipmentSystem syncs these to Weapon/Shield components each frame.
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

    // EquipmentSystem compares these to detect slot changes.
    // Sentinel ensures the first update always triggers sync (even for fists).
    std::string synced_right_hand = "__unsynced__";
    std::string synced_left_hand = "__unsynced__";
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

// DeathSound -- per-entity sound played once when the entity dies.
struct DeathSound
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

// AggroSound -- sounds played on a random timer while the entity is aggro'd
// (AIController::State != Idle). First sound plays immediately on aggro.
struct AggroSound
{
    std::vector<std::string> paths;
    float volume = 0.4f;
    float min_interval = 3.0f;
    float max_interval = 8.0f;
    float max_distance = 400.0f;
    float min_pitch = 0.85f;
    float max_pitch = 1.15f;
    float timer = 0.0f;
    bool was_aggro = false; // tracks state transition for immediate first sound
    int shuffle_index = 0;
    std::vector<int> shuffle_order;
    int voice = -1; // tracked voice index for stopping on hit
};

// Projectile -- a moving damage entity (bullet, arrow) spawned by CombatSystem.
// ProjectileSystem manages lifetime, wall destruction, and pierce logic.
struct Projectile
{
    entt::entity owner = entt::null;
    float max_range = 500.0f;
    float spawn_x = 0.0f;
    float spawn_y = 0.0f;
    int pierce_remaining = 0;
    float dir_x = 0.0f;
    float dir_y = 0.0f;
    float speed = 0.0f;
    bool left_hand = false;
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
