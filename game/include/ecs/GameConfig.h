#pragma once

#include "GameComponents.h"

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
// MusicConfig -- background music tracks, loaded from config/audio/music.json.
// ---------------------------------------------------------------------------
struct MusicConfig
{
    struct Track
    {
        std::string path;
        float volume = 0.6f;
    };
    std::vector<Track> tracks;
    float default_volume = 0.6f;
    int last_track_index = -1; // avoid repeating the same track back-to-back
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

    Phase phase = Phase::SafeRoom;
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

    // Armor-specific (only meaningful when category == Armor).
    ArmorSlot armor_slot = ArmorSlot::Chest;
    float defense_bonus = 0.0f;
    float poise_bonus = 0.0f;

    // Shield (armor in off-hand; max_guard > 0 means this is a shield).
    float max_guard = 0.0f;

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
// UIState -- tracks which UI screen is currently open.
// ---------------------------------------------------------------------------
struct UIState
{
    enum class Screen
    {
        None,
        Menu,
        LevelUp
    };

    // Menu tabs.
    enum class Tab
    {
        Status = 0,
        Inventory = 1,
        Equipment = 2,
        Crafting = 3
    };
    static constexpr int TAB_COUNT = 4;

    Screen active_screen = Screen::None;
    Tab menu_tab = Tab::Status;
    bool show_hud = true;

    bool isScreenOpen() const
    {
        return active_screen != Screen::None;
    }
};
