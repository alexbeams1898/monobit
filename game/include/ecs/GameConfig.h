#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Game config data -- balance, audio, and wave configuration.
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
        float scale = 100.f;
    } hp;

    struct
    {
        float base = 150.f;
        float dex_scale = 30.f;
        float sprint_multiplier = 1.6f;
        float sprint_blend = 8.0f;
        float walk_blend = 20.0f;
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
        float weight_scale = 100.f;
        float stat_scale = 160.f;
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
        float xp_base = 100.f;
        float xp_exponent = 1.5f;
        float points_per_level = 1.f;
    } leveling;

    struct
    {
        float log_scale = 1.5f;
        // XP multiplier = max(min_fraction, enemy_level / player_level).
        // Kills on lower-level enemies pay less XP; floor prevents 0.
        float min_fraction = 0.1f;
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
        float swing_effort = 3.0f;
        float dodge_effort = 5.0f;
        float skill_effort = 4.0f;
        float sprint_effort = 1.0f;
        float base = 5.0f;
        float end_scale = 3.0f;
        float recovery_rate = 2.5f;
        float recovery_delay = 1.0f;
        float exhaustion_stagger = 0.6f;
    } stamina;

    bool loaded = false;
};

// ---------------------------------------------------------------------------
// SoundConfig -- event-to-file mappings loaded from config/audio/sounds.json.
// ---------------------------------------------------------------------------
struct SoundEntry
{
    std::string path;
    float volume = 0.5f;
};

struct SoundConfig
{
    SoundEntry player_attack{"assets/sfx/attack.wav", 0.5f};
    SoundEntry player_skill{"assets/sfx/skill.wav", 0.6f};
    SoundEntry player_dodge{"assets/sfx/dodge.wav", 0.5f};
    SoundEntry hit{"assets/sfx/hit.wav", 0.4f};
    SoundEntry parry{"assets/sfx/parry.wav", 0.6f};
    SoundEntry death{"assets/sfx/death.wav", 0.5f};
    SoundEntry pickup{"assets/sfx/pickup.wav", 0.4f};
    SoundEntry level_up{"assets/sfx/levelup.wav", 0.6f};
    SoundEntry stat_allocate{"assets/sfx/stat_allocate.wav", 0.5f};
    SoundEntry wall_bump{"assets/sfx/wall_bump.wav", 0.3f};
    SoundEntry footstep_walk{"assets/sfx/footstep_walk.wav", 0.15f};
    SoundEntry footstep_run{"assets/sfx/footstep_run.wav", 0.25f};
    SoundEntry rest_heal{"assets/sfx/rest_heal.wav", 0.5f};
    SoundEntry game_over{"assets/sfx/game_over.wav", 0.6f};
    SoundEntry low_stamina_heartbeat{"assets/sfx/heartbeat.wav", 0.5f};
    bool loaded = false;
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
        Spawning,
        Active,
        Cleared,
        SafeRoom,
        GameOver,
        Complete
    };

    Phase phase = Phase::Idle;
    int current_wave = 0;
    int enemies_spawned = 0;
    int enemies_total = 0;
    float spawn_timer = 0.0f;
    int spawn_group_index = 0;
    int spawn_group_progress = 0;

    // Set by startNextWave; consumed by GameLoop to regen the tile map.
    bool needs_map_regen = false;

    ActiveWave active_def;
};
