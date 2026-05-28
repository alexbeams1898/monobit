#pragma once

#include <string>
#include <vector>

// RPG-progression components. Ported from games/prison-escape-game/ to the
// engine layer for reuse. Pure data, no logic. Systems that act on these
// (combat, damage, leveling, weapon XP, equipment) live in each game.

namespace engine::ecs
{

// Base stats. Allocated by the player via level-up.
struct Stats
{
    int str = 1;
    int dex = 1;
    int end = 1;
    int lck = 1;
};

// Physical material properties of a creature. Not levelable. Represents
// what the creature IS made of (bone, flesh, demon hide, etc.). Includes
// natural-weapon stats (unarmed attack derived from body composition).
struct Body
{
    int base_hp = 0;
    int base_defense = 0;

    // Natural weapon (unarmed attack).
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

// Level progression + unspent stat-allocation points.
struct Experience
{
    int current_xp = 0;
    int xp_to_next = 100;
    int level = 1;
    int stat_points = 0;
};

// Equipped-weapon runtime state. Synced from the equipped ItemDef each frame
// by the game's equipment system. Distinct from ItemDef so the renderer +
// combat path can poke at this without re-resolving the registry every frame.
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

    // Ranged.
    bool ranged = false;
    float projectile_speed = 400.0f;
    float effective_range = 500.0f;
    float spread = 0.0f;
    int projectile_count = 1;
    float projectile_size = 6.0f;
    int pierce = 0;
    std::string projectile_sprite;
    std::string ammo_type;
    std::string fire_sound;
    float fire_rate = 0.0f;
    float stamina_cost = -1.0f;

    // Visual fields (2D-specific; ignored by 3D games).
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
    std::string attack_anim;
    std::vector<int> shoot_frames;

    // Two-handed state.
    bool two_handed = false;
    bool two_handed_active = false;

    // Per-weapon XP.
    int wxp_level = 1;
    float wxp_current = 0.0f;
    float wxp_to_next = 50.0f;
};

// Left-hand weapon. Inherits Weapon so ECS frameworks can store both hands
// as distinct types on the same entity.
struct LeftWeapon : Weapon
{
};

// Tracks weapon leveling through combat use. Attached when relevant.
struct WeaponXP
{
    int level = 1;
    float current_xp = 0.0f;
    float xp_to_next = 50.0f;
};

// Shield. Either hand can hold one; blocking gated on the holding hand.
struct Shield
{
    float guard_health = 100.0f;
    float max_guard = 100.0f;
    bool blocking = false;
};

// Aggregated defensive stats from all equipped armor. Computed by the
// game's equipment system each time equipment changes.
struct ArmorStats
{
    float total_defense = 0.0f;
    float total_poise_bonus = 0.0f;
    float total_weight = 0.0f;
    float equip_load_ratio = 0.0f;
    int load_tier = 0; // 0=light 1=medium 2=heavy 3=overloaded
};

// Brief invulnerability + stagger window opened by a timed block.
struct Parrying
{
    float remaining = 0.0f;
};

// Hits absorbed before staggering.
struct Poise
{
    float max = 0.0f;
    float current = 0.0f;
    float decay_timer = 0.0f;
};

// Real resource pool depleted by combat actions + sprint. sprint_locked
// fires when the bar hits 0; cleared when fully recovered. Forces a full
// bar before sprinting resumes (the canonical soulslike stamina rule).
struct Stamina
{
    float current = 0.0f;
    float max_stamina = 0.0f;
    float recovery_timer = 0.0f;
    bool sprint_locked = false;
};

} // namespace engine::ecs
