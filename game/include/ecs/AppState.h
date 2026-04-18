#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Application state, UI state, run statistics, scoring, and persistence.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// UIState -- tracks which UI screen is currently open.
// ---------------------------------------------------------------------------
struct UIState
{
    enum class Screen
    {
        None,
        Menu,
        LevelUp,
        Sanctuary,
        Crafting
    };

    // Menu tabs.
    enum class Tab
    {
        Status = 0,
        Inventory = 1,
        Equipment = 2
    };
    static constexpr int TAB_COUNT = 3;

    Screen active_screen = Screen::None;
    Tab menu_tab = Tab::Status;
    bool show_hud = true;
    bool input_suppressed = false; // set when a screen closes to block one frame of input

    bool isScreenOpen() const
    {
        return active_screen != Screen::None;
    }
};

// ---------------------------------------------------------------------------
// GameState -- top-level application state machine.
// UIState.Screen handles in-game overlays (pause menu, level-up).
// GameState controls which major application mode is active.
// ---------------------------------------------------------------------------
struct GameState
{
    enum class Phase
    {
        MainMenu,
        CharCreate,
        LoadGame,
        Playing,
        Victory,
        GameOver,
        RunSummary,
        HighScores,
        Controls,
        Settings
    };

    Phase phase = Phase::MainMenu;
    bool world_initialized = false;
    bool pending_world_create = false; // defers createWorld to gameUpdate for loading overlay
    std::string active_character;      // name of the character for the current run
};

// ---------------------------------------------------------------------------
// RunStats -- accumulated statistics for the current run.
// Stored in registry ctx, reset at the start of each new game.
// ---------------------------------------------------------------------------
struct RunStats
{
    int kills = 0;
    float time = 0.0f;
    int wave = 0;
    int xp_earned = 0;
    int money = 0;
    int score = 0;
};

// ---------------------------------------------------------------------------
// ScoringConfig -- configurable score formula weights.
// Loaded from config/balance/scoring.json.
// ---------------------------------------------------------------------------
struct ScoringConfig
{
    float kill_weight = 10.0f;
    float wave_weight = 100.0f;
    float time_penalty_weight = 0.5f;
    float xp_weight = 1.0f;
    float money_weight = 2.0f;
    float escape_multiplier = 1.5f;
    bool loaded = false;
};

// ---------------------------------------------------------------------------
// PlayerProfile -- persistent character identity.
// ---------------------------------------------------------------------------
struct PlayerProfile
{
    std::string name;
    int money = 0;
    std::unordered_map<std::string, std::string> appearance; // category_id -> option_id
};

// ---------------------------------------------------------------------------
// Run -- one completed run's snapshot.
// ---------------------------------------------------------------------------
struct Run
{
    RunStats stats;
    std::string character_name;
    std::string timestamp;
    bool escaped = false;
    // Run was completed with god mode enabled. Such runs are excluded from the
    // high-score leaderboard but still recorded so the player can review their
    // own history. Defaults to false so legacy save files load correctly.
    bool god_mode = false;
};

// ---------------------------------------------------------------------------
// SaveData -- top-level persistent data. Loaded/saved to saves/save.json.
// Schema version enables forward-compatible migrations.
// ---------------------------------------------------------------------------
struct SaveData
{
    static constexpr int CURRENT_VERSION = 2;

    int schema_version = CURRENT_VERSION;
    std::vector<PlayerProfile> characters;
    std::vector<Run> runs;
    bool god_mode = false;
};

// ---------------------------------------------------------------------------
// DebugFlags -- runtime debug toggles (F6 = god mode, etc.).
// Stored in registry ctx. Never persisted to save files.
// ---------------------------------------------------------------------------
struct DebugFlags
{
    bool god_mode = false;
};
