#pragma once

#include <entt/entt.hpp>
#include <string>

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

// Stats -- base stats for any entity.
struct Stats
{
    int str = 1;
    int dex = 1;
    int end = 1;
    int lck = 1;
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

// Loot -- reward data dropped when an entity dies.
struct Loot
{
    int xp_drop = 20;
    int money_drop = 0;
    int level = 1;
};

// Pickup -- an XP or money drop left by dead enemies.
struct Pickup
{
    int xp_value = 0;
    int money_value = 0;
    float radius = 48.0f;
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
