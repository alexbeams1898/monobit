#pragma once

#include "items/Inventory.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Application state and persistence structures for Selva Oscura.
//
// Modeled on games/prison-escape-game/include/ecs/AppState.h. Many of the
// patterns (Phase enum + per-phase handler, UIState overlay, schema-versioned
// SaveData) are reused verbatim from prison-escape; Selva-specific values
// (Phase variants, schema fields) are scoped to Selva's cosmology.
//
// v1 schema is intentionally minimal: a character is just a name. The Seal
// moment (class pick vs unburdened, per setting.md) happens in the opening
// sequence inside Hell, not at the main menu, so the main-menu-side
// character-create only needs to elicit a name. Class, stats, sangue totals,
// evolution stage, keepers felled, etc. are added in later schema bumps as
// those systems ship.
// ---------------------------------------------------------------------------

namespace selva
{

// ---------------------------------------------------------------------------
// UIState - in-game overlay tracking. The pause menu and its tabs (Status,
// Inventory, Equipment, System) live here. GameState owns the top-level
// application mode; UIState owns the in-game overlay layered on top of
// Playing. Layout follows Elden Ring's convention - gameplay-data tabs
// (Status, Inventory, Equipment) and a System tab that holds Save / Settings
// / Quit-to-menu / Quit-to-desktop. Resume is universally ESC + RMB - not
// a button in any tab.
// ---------------------------------------------------------------------------
struct UIState
{
    enum class Screen
    {
        None,
        Menu,
    };

    enum class Tab
    {
        Status = 0,
        Inventory = 1,
        Equipment = 2,
        System = 3,
    };
    static constexpr int TAB_COUNT = 4;

    Screen active_screen = Screen::None;
    Tab menu_tab = Tab::Status;
    bool show_hud = true;
    bool input_suppressed = false;

    // SDL_GetTicks64() value at the moment the last autosave completed.
    // Used by the save indicator chip on the HUD. We use SDL ticks here
    // (not selva::wallClock()) because the indicator must keep counting
    // down even when gameplay is paused - selvaPerFrame is gated off
    // during pause, so the gameplay wallclock freezes. 0 = no save this
    // session.
    std::uint64_t last_save_ticks_ms = 0;

    bool isScreenOpen() const
    {
        return active_screen != Screen::None;
    }
};

// ---------------------------------------------------------------------------
// GameState - top-level application-mode state machine. Each value names a
// distinct rendering and update path. The main loop dispatches on
// GameState::phase; screens (MainMenu, CharCreate, LoadGame, Settings) are
// stateless renderers that return an Action which transitions the phase.
//
// Selva phases differ from prison-escape: no Victory/GameOver/HighScores/
// RunSummary - Selva is roguelike, run-end loops back into Playing through
// a Wood-respawn rather than terminating. Those phases will be added when
// run-end + cycle structure ships.
// ---------------------------------------------------------------------------
struct GameState
{
    enum class Phase
    {
        MainMenu,
        CharCreate,
        LoadGame,
        Settings,
        Playing,
    };

    Phase phase = Phase::MainMenu;
    bool world_initialized = false;
    // Defers world creation by one tick after the player selects new/load
    // game, so the loading overlay can render before the world spins up.
    bool pending_world_create = false;
    // Set true ONLY on the New-Game path (not Load-Game). Consumed by
    // selvaPerFrame on the first frame after world creation to fire the
    // wake-up animation Scene. The Vagrant wakes only on the first
    // arrival of a save; subsequent respawns place him standing.
    bool pending_wake_scene = false;
    std::string active_character; // Name of the character for the current run.

    // --- Boss-encounter active state (per docs/design/ideas/boss_backend.md
    // section 6) ---

    // Pool index of the actor that's the active boss this frame, or -1
    // if no boss is engaged. Stored as INDEX (not pointer) because the
    // actor pool may resize between frames; resolve to pointer at use
    // site via selva::gameplay::actors()[active_boss_idx]. Set when an
    // engage trigger fires (or a SpawnEntity trigger spawns a boss);
    // cleared when the boss dies (post-felled-overlay).
    int active_boss_idx = -1;

    // The active boss's spawn-decl id (e.g. "lupa"). Stable across
    // frames even if the pool resizes -- used as the persistent key
    // for save's felled_bosses list when the boss dies. Empty when no
    // boss engaged.
    std::string active_boss_id;
};

// ---------------------------------------------------------------------------
// PlayerProfile - persistent character identity. Schema starts minimal (just
// a name) and grows via schema_version bumps as more systems ship.
//
// Anticipated future fields (not yet in schema; documented for reference):
//   - class:                Penitent / Heretic / Wretched / Unburdened
//     (per setting.md - set during opening sequence at the Seal moment)
//   - path:                 class-picker | unburdened (derived from class)
//   - evolution_stage:      L1/L2/L3 for class-pickers, Unburdened/Svuotato/
//                           Diaphanous for unburdened
//   - stats:                HP / fire_rate / damage (per CLAUDE.md three-stat
//                           constraint)
//   - lifetime_sangue:      cumulative sangue collected across all cycles
//   - lifetime_riversato:   cumulative sangue poured out (unburdened only)
//   - keepers_felled:       set of "Charon", "Minos", etc. - per setting.md
//                           Per-circle reactivity (drives world-state)
//   - wood_marks:           cairns, etched names, riversamento sites placed
//                           in the Wood (per wood.md persistence)
//   - grimoire_unlocks:     list of Grimoire entries the player has seen
// ---------------------------------------------------------------------------
struct PlayerProfile
{
    std::string name;

    // Last position + facing yaw when the character was saved. Used to
    // restore where the player was on Playing-enter (quit-to-menu and
    // resume returns you to where you were, not to the world spawn).
    // `has_saved_pose` distinguishes "new character, no save yet" from
    // "character saved with literal (0,0,0)". Without the flag, a
    // newly-created character would resume at world origin instead of
    // the configured spawn point.
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float pos_z = 0.0f;
    float yaw = 0.0f;
    bool has_saved_pose = false;

    // Region the player was in when saved. Player pos above is in this
    // region's local coordinate space. Missing / empty = "surface"
    // (back-compat for save files written before the Regions system
    // existed). When the region-aware load runs:
    //   1. activateRegionImmediate(findRegionId(current_region_id))
    //   2. teleport player capsule to (pos_x, pos_y, pos_z), yaw
    std::string current_region_id;

    // Bosses this character has felled. Per
    // games/selva-oscura/docs/design/ideas/boss_backend.md section 5.
    // When a boss actor dies (`actor.is_boss && actor.is_dead`) its
    // spawn-decl id is appended here. On region load, any
    // `enemy_spawns[]` entry whose id appears in this list is SKIPPED
    // entirely -- the boss does not respawn, the slope stays empty
    // for the rest of the save. Per [[selva-wood-lore-locked-2026-05-31]]
    // the empty slope IS the monument.
    //
    // Future: this list may merge with `keepers_felled` (per setting.md
    // *Cycle structure*) since keepers ARE a subset of bosses. Kept
    // separate for v1 until the first keeper ships.
    std::vector<std::string> felled_bosses;

    // Generic quest-state flag set. Persistent string set. Used by:
    //   - Dialogue trees (gate branches on flag presence)
    //   - NPC spawn conditions (only spawn if flag X is set)
    //   - Scripted-event triggers (fire X once if flag Y not set)
    //   - Future: achievement system (derive Steam/PSN achievements
    //     from declarative conditions over this set)
    // Names are STABLE identifiers (renaming = breaking a save); use
    // semantic prefixes like "lupa_felled", "met_guide", "grimoire_5".
    // Access through hasFlag / setFlag / clearFlag in AppStateGlobal.h
    // -- those normalize dedup behavior so direct push_back is never
    // necessary. Empty = no flags set yet (back-compat default for
    // saves written before the flag system shipped).
    std::vector<std::string> flags;

    // Persistent door state. (door_id, state_name) pairs. Only doors
    // whose state has DEVIATED from their JSON-authored initial_state
    // need entries here. State_name is one of "Locked", "Closed",
    // "Open" (Opening is promoted to Open on save -- mid-animation
    // doesn't persist). Per [[world/Door.h]].
    std::vector<std::pair<std::string, std::string>> door_states;

    // Per-character carried items. Categorized polymorphic entries
    // (Possession / Stack / Instanced) keyed by category id from
    // config/inventory_categories.json. Operate via selva::items
    // ops (grant/addStack/has/remove). Empty for new characters.
    selva::items::Inventory inventory;
};

// ---------------------------------------------------------------------------
// Settings - persistent user preferences. Lives at the save level (not
// per-character) because settings apply to the whole install.
// ---------------------------------------------------------------------------
struct Settings
{
    float bgm_volume = 0.8f;
    float sfx_volume = 1.0f;
    // Camera FOV in degrees. Separate values for third-person (default
    // 60, narrow soulslike framing) and first-person (default 75,
    // wider for spatial awareness when the player can't see their own
    // body). User-tunable via the Settings screen.
    float fov_degrees_third_person = 60.0f;
    float fov_degrees_first_person = 75.0f;
};

// ---------------------------------------------------------------------------
// SaveData - top-level persistent data. Serialized to JSON at
// %APPDATA%/SelvaOscura/save.json (Windows) or platform equivalent via
// SDL_GetPrefPath. Schema versioning enables forward-compatible migrations.
// ---------------------------------------------------------------------------
struct SaveData
{
    static constexpr int CURRENT_VERSION = 2;

    int schema_version = CURRENT_VERSION;
    std::vector<PlayerProfile> characters;
    Settings settings;
};

} // namespace selva
