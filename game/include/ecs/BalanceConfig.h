#pragma once

#include "ecs/Components.h"

#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Balance, audio, and wave configuration.
// Pure data containers with no logic. Stored in entt::registry::ctx() at
// runtime. Loaded once by ConfigLoader at startup, read-only during gameplay.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// FormulaConfig -- all balance constants loaded from config/balance/formulas.json.
// ---------------------------------------------------------------------------
struct FormulaConfig
{
    struct
    {
        float base = 5.f;
        float scale = 15.f;
        float level_scale = 5.f;
    } hp;

    struct
    {
        float base = 150.f;
        float dex_scale = 30.f;
        float sprint_multiplier = 1.6f;
        float sprint_blend = 8.0f;
        float walk_blend = 20.0f;
        float backpedal_multiplier = 0.5f;
        float sprint_anim_speed = 0.65f;
        float backpedal_anim_speed = 1.4f;
    } movement;

    struct
    {
        float str_scale = 20.f;
        float end_scale = 10.f;
    } carry_weight;

    struct
    {
        float str_scale = 0.3f;
        float end_scale = 0.5f;
        float level_scale = 0.2f;
        float cap = 75.f;
    } defense;

    struct
    {
        float drop_scale = 15.f;
        float quality_scale = 3.f;
        float essence_quality_scale = 0.01f;
        float quality_thresholds[4] = {55.f, 85.f, 102.f, 120.f};
    } luck;

    struct
    {
        float s = 1.5f;
        float a = 1.25f;
        float b = 1.0f;
        float c = 0.75f;
        float d = 0.5f;
        float e = 0.25f;
    } grade_thresholds;

    struct
    {
        float base_swing_time = 0.8f;
        float weight_scale = 0.5f;
        float stat_scale = 80.f;
        float two_handed_str_bonus = 0.3f;
    } swing;

    struct
    {
        float penalty_rate = 0.15f;
    } stat_requirement;

    struct
    {
        float end_scale = 2.0f;
        float str_scale = 1.0f;
        float weight_scale = 3.0f;
        float stagger_duration = 0.15f;
        float decay_window = 5.0f;
    } poise;

    struct
    {
        float xp_base = 0.069f;
        float xp_exponent = 3.5f;
        float xp_offset = 7.f;
        float points_per_level = 1.f;
    } leveling;

    struct
    {
        float log_scale = 1.5f;
        // XP multiplier = max(min_fraction, 1 - level_penalty * level_diff).
        // Gentle linear falloff per level the player is above the enemy.
        float min_fraction = 0.1f;
        float level_penalty = 0.15f;
        float essence_scale = 0.005f;
    } xp_drop;

    struct
    {
        float duration = 0.25f;
        float cooldown = 0.35f;
    } dodge;

    struct
    {
        int min = 0;
        int max = 100;
    } essence;

    struct
    {
        float base_swing_cost = 3.0f;
        float swing_effort = 0.5f;
        float dodge_effort = 2.5f;
        float skill_effort = 4.0f;
        float sprint_effort = 2.0f;
        float sprint_dex_scale = 0.15f;
        float base = 10.0f;
        float end_scale = 20.0f;
        float recovery_rate = 8.0f;
        float recovery_delay = 1.0f;
        float exhaustion_stagger = 0.6f;
    } stamina;

    struct
    {
        float weight = 0.5f;
        float base_damage = 5.0f;
        float str_scaling = 1.0f;
        float dex_scaling = 0.75f;
    } fist;

    struct
    {
        float kill_multiplier = 1.0f;
        float hit_multiplier = 0.05f;
        float crit_multiplier = 0.3f;
        float base_xp = 50.0f;
        float exponent = 2.0f;
        float growth_bonus_per_quality = 0.1f;
        float decay_rate = 0.05f;
        float carry_factor = 0.15f;
        // Weapon power XP scaling: effective_rate = tier_rate * (power_base + base_damage *
        // power_dmg_factor + rarity * power_rarity_factor).
        float power_base = 0.1f;
        float power_dmg_factor = 0.03f;
        float power_rarity_factor = 0.1f;
        // Enemy power rating weights.
        float power_level_weight = 1.0f;
        float power_hp_weight = 0.1f;
        float power_dmg_weight = 0.5f;
        float power_stat_weight = 0.2f;
    } weapon_xp;

    struct
    {
        float base_capacity = 40.0f;
        float str_scale = 3.0f;
        float end_scale = 1.5f;
        float light_threshold = 0.3f;
        float medium_threshold = 0.7f;
        float heavy_threshold = 1.0f;
        float light_speed = 1.0f;
        float medium_speed = 0.9f;
        float heavy_speed = 0.7f;
        float overloaded_speed = 0.4f;
    } equip_load;

    struct
    {
        int max_attack_tokens = 2;
        float wait_radius_mult = 2.0f;
        float waiter_speed_scale = 0.15f;
        float kite_speed_threshold = 0.5f;
        float chase_spread = 0.15f;
        float slot_rotation_speed = 0.5f;
        float min_slot_gap = 0.8f;
        float attack_arrival_dist = 16.0f;
        float slot_arrive_dist = 24.0f;
        float engagement_radius = 150.0f;
        float enemy_reach = 24.0f;
    } combat_ai;

    struct
    {
        float attack_lock_fraction = 0.6f;
        float normal_reach = 36.0f;
        float skill_reach = 56.0f;
        float normal_hitbox_size = 32.0f;
        float skill_hitbox_size = 64.0f;
        float skill_damage_mult = 1.5f;
        float skill_cooldown = 5.0f;
        float skill_lock_duration = 0.4f;
        float dodge_speed = 300.0f;
        float parry_window = 0.15f;
        float backstab_threshold = -0.3f;
        float backstab_multiplier = 2.0f;
        float riposte_multiplier = 2.5f;
        float riposte_window = 0.8f;
        float critical_lock_duration = 0.6f;
        float lock_on_range = 300.0f;
        // Hitstop: brief game-logic pause on damage land. ~3 frames at 60fps.
        float hitstop_seconds = 0.05f;
    } combat;

    bool loaded = false;
};

// ---------------------------------------------------------------------------
// SoundConfig -- event-to-file mappings loaded from config/audio/sounds.json.
// Fully data-driven: every key in the JSON becomes an entry in the map.
// Adding a new sound = adding a key to sounds.json. No C++ changes needed.
// ---------------------------------------------------------------------------
struct SoundEntry
{
    std::string path;
    float volume = 0.5f;
    std::vector<std::string> variations;
};

struct SoundConfig
{
    std::unordered_map<std::string, SoundEntry> entries;
    bool loaded = false;

    // Lookup by key. Returns a static empty entry if key not found.
    const SoundEntry& get(const std::string& key) const
    {
        auto it = entries.find(key);
        if (it != entries.end())
            return it->second;
        static const SoundEntry empty{"", 0.0f, {}};
        return empty;
    }

    bool hasKey(const std::string& key) const
    {
        return entries.count(key) != 0;
    }
};

// ---------------------------------------------------------------------------
// MusicConfig -- background music tracks, loaded from config/audio/music.json.
// ---------------------------------------------------------------------------
struct MusicConfig
{
    struct Track
    {
        std::string path;
        float volume = 0.6f;
        int fade_in_ms = 0;
    };
    std::vector<Track> tracks;
    std::unordered_map<std::string, Track> named;
    float default_volume = 0.6f;
    int last_track_index = -1; // avoid repeating the same track back-to-back
    float main_menu_rare_chance = 0.0f;

    const Track* get(const std::string& key) const
    {
        auto it = named.find(key);
        return (it != named.end() && !it->second.path.empty()) ? &it->second : nullptr;
    }
};

// ---------------------------------------------------------------------------
// WaveConfig -- auto-wave generation rules loaded from config/waves.json.
// ---------------------------------------------------------------------------

struct WaveEnemyEntry
{
    std::string config_path;
    int from_wave = 1;
    int weight = 1;
};

struct WaveOverride
{
    struct Group
    {
        std::string config_path;
        int count = 1;
    };
    std::vector<Group> enemies;
    float spawn_interval = 0.5f;
    int burst_size = 1;
    bool safe_room_after = false;
};

struct WaveGenRules
{
    std::vector<WaveEnemyEntry> enemies;
    int start_count = 3;
    float count_growth = 1.5f;
    int max_count = 200;
    float start_interval = 1.0f;
    float interval_decay = 0.95f;
    float min_interval = 0.15f;
    int start_burst = 1;
    int burst_growth_every = 5;
    int max_burst = 10;
    int safe_room_every = 0;
    int max_waves = 0;
    float level_growth = 0.5f;
    int stat_per_level = 1;
    std::unordered_map<int, WaveOverride> overrides;
};

struct ActiveWave
{
    struct Group
    {
        std::string config_path;
        int count = 1;
    };
    std::vector<Group> enemies;
    float spawn_interval = 0.5f;
    int burst_size = 1;
    bool safe_room_after = false;
};

struct WaveConfig
{
    WaveGenRules gen;
    float spawn_near = 330.0f;
    float spawn_far = 825.0f;
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// WaveState -- runtime state for the wave manager.
// ---------------------------------------------------------------------------
struct WaveState
{
    enum class Phase
    {
        Idle,
        Transitioning, // teleport SFX playing, brief delay before spawning
        Spawning,
        Active,
        Cleared,
        SafeRoom,
        GameOver,
        Complete
    };

    Phase phase = Phase::SafeRoom;
    int current_wave = 0;
    int enemies_spawned = 0;
    int enemies_total = 0;
    float spawn_timer = 0.0f;
    int spawn_group_index = 0;
    int spawn_group_progress = 0;

    float cleared_timer = 0.0f;    // countdown before auto-advancing to next wave
    float transition_timer = 0.0f; // countdown during Transitioning phase

    // Set by startNextWave; consumed by GameLoop to regen the tile map.
    bool needs_map_regen = false;

    ActiveWave active_def;
};

// ---------------------------------------------------------------------------
// AnimRowIndex -- maps animation state names (e.g. "thrust", "shoot") to their
// row data from the animation config JSON. Populated once by ConfigLoader.
// Used by AnimStateSystem to resolve weapon-specific attack rows at runtime.
// ---------------------------------------------------------------------------
struct AnimRowEntry
{
    int row = 0;
    int frames = 1;
    float duration = 0.0f;
};

struct AnimRowIndex
{
    std::unordered_map<std::string, AnimRowEntry> rows;
};

// ---------------------------------------------------------------------------
// HandAnchorData -- per-frame hand positions for weapon sprite placement.
// Pixel offsets from the 64x64 frame center (32, 32). Positive x = right on
// screen, positive y = down.
//
// Each row stores BOTH the player's left hand and (optionally) right hand per
// direction per frame. Two-handed weapons use both; one-handed weapons only
// read the primary (left) hand. The right hand is optional: if an animation
// row has no right-hand data, the game silently falls back to 1H rendering
// even for 2H-capable weapons.
// ---------------------------------------------------------------------------
struct HandAnchor
{
    float x = 0.0f;
    float y = 0.0f;
    float rotation = -999.0f; // weapon rotation in radians; -999 = use default table
    int flip = -1;            // 0=no flip, 1=flip; -1 = use default table
    int depth = 0;            // sub_layer override; 0 = use depth_per_dir
};

struct HandAnchorRow
{
    // left[dir][frame] -- dir: 0=South, 1=West, 2=East, 3=North
    // Player's left hand (trigger hand for 1H weapons).
    std::vector<std::vector<HandAnchor>> left;
    // right[dir][frame] -- same layout as left; may be empty if not measured.
    // Used only for two-handed weapon rendering.
    std::vector<std::vector<HandAnchor>> right;
};

struct HandAnchorData
{
    // row index (animation row) -> anchor data for that row
    std::unordered_map<int, HandAnchorRow> rows;
    // Per-direction weapon depth: +1 = in front of body, -1 = behind body
    // Index: 0=South, 1=West, 2=East, 3=North
    std::vector<int> depth_per_dir = {1, 1, 1, -1};
};

// ---------------------------------------------------------------------------
// AttackAnimHitbox -- per-attack-row hitbox timing + positioning.
//
// An attack row (slash, thrust, etc.) declares which frames are active
// (produce hits) and for each active frame, an (x, y, rotation) offset
// applied to the wielder's facing direction. The weapon's hitbox shapes are
// transformed by this per-frame offset, so the shape sweeps through space
// as the animation plays -- this is what gives different attacks different
// arcs (slash = wide sweep, thrust = forward push, overhead = vertical arc).
//
// Coordinates are in attacker-local space: +x along facing, +y perpendicular
// (right of facing). HitboxResolverSystem rotates by the wielder's facing
// direction when testing against hurtboxes.
// ---------------------------------------------------------------------------
struct HitboxKeyframe
{
    int frame = 0;         // frame index within the attack row
    float x = 0.0f;        // attacker-local offset along facing
    float y = 0.0f;        // attacker-local offset perpendicular to facing
    float rotation = 0.0f; // radians; applied to each weapon shape about its origin
};

struct AttackAnimHitbox
{
    std::vector<HitboxKeyframe> keyframes;
    // Multi-hit support: if > 0, the same hurtbox may be re-hit after this
    // many seconds from its last hit by this attack_id. -1 = single hit only.
    float hit_interval = -1.0f;
};

struct AttackAnimHitboxData
{
    // attack_anim name (e.g. "slash", "thrust") -> keyframe set
    std::unordered_map<std::string, AttackAnimHitbox> attacks;
};

// AnimSheetHurtboxes -- hurtbox shapes declared in an animation config JSON,
// keyed by the sheet path (e.g. "config/animations/lpc_humanoid.json"). All
// entities using that animation sheet inherit the shape set automatically,
// so we only author humanoid hurtboxes once. Per-entity overrides in the
// entity JSON's "hurtbox" component field still take precedence.
struct AnimSheetHurtboxes
{
    std::unordered_map<std::string, std::vector<HurtShape>> by_sheet;
};
