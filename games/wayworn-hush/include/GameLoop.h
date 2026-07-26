#pragma once

#include "AppState.h"
#include "Crafting.h"
#include "Footsteps.h"
#include "Formulas.h"
#include "Glimmer.h"
#include "Growth.h"
#include "HeadMarker.h"
#include "HudCanvas.h"
#include "InteractionMode.h"
#include "Inventory.h"
#include "LdtkImport.h"
#include "Notebook.h"
#include "Observations.h"
#include "PlayerConfig.h"
#include "Settings.h"
#include "Structures.h"
#include "Surfaces.h"
#include "WatchHud.h"
#include "WorldClock.h"
#include "WorldConfig.h"
#include "WorldItems.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

class Engine;
class EntityManager;

// The pause page: the game's one on-demand screen, opened with F. It IS the
// growth/observation record -- there is no persistent HUD (see
// docs/design/GAME-SYSTEMS.md). While open the world update freezes and the
// soundtrack is muffled. Tabs: Self (Spirit + faculties), Satchel (carried items),
// Craft, Notebook (the thoughts he's had), and System (a Controls sub-view + Quit;
// later settings/save). Closing is F -- there is no "Resume" item.
struct PauseState
{
    // System is the last tab (its items open sub-views / exit); the page opens
    // on Self. Satchel = what you carry; Notebook = every thought there is to have,
    // the found ones legible and the rest still blank.
    enum class Tab
    {
        Self,
        Satchel,
        Craft,
        Notebook,
        System
    };

    // Sub-views reached from a System-tab item. The page is a back-stack: the
    // tab strip is the base; opening one of these pushes it; Back (F) pops. New
    // sub-pages (Settings, Save) are just new enumerators + a render branch --
    // no new navigation plumbing.
    enum class View
    {
        Controls
    };

    bool open = false;
    Tab tab = Tab::Self;
    // Empty = showing the tab strip. Each entry is a pushed sub-view; the last
    // is the one on screen. Back pops one level (closing the page when empty).
    std::vector<View> view_stack;
    // Selected System-tab item: -1 = none (nothing highlighted until the player
    // hovers or presses W/S), 0 = Controls, 1 = Quit.
    int system_sel = -1;

    // Satchel grid cursor (read-only; drives the detail panel).
    int satchel_sel = 0;

    // Notebook list cursor (read-only; drives the detail panel). Indexes the tab's
    // rows -- every authored thought, found or not -- so a blank slot can be selected
    // and read as blank.
    int notebook_sel = 0;

    // Craft tab: the Satchel material list + a trailing "Combine" control. `craft_sel` is the
    // highlighted row (0..n-1 materials, then n = Combine). `craft_selected` is what's in the pot
    // -- item id -> how many of it the player has thrown in (each click adds one, up to how many
    // they carry; clicking a pot row takes one back). The attempt's feedback (made item / near-miss
    // / short-on-materials) surfaces as a notification toast, not a line held here.
    int craft_sel = 0;
    std::unordered_map<std::string, int> craft_selected;
};

// Game-wide runtime state, stored as a singleton in the entt registry context
// (reg.ctx()). Systems read it from there rather than from file-scope globals.
struct GameState
{
    app::State app; // what the program is doing (greeting / playing); gates the world tick
    entt::entity player = entt::null;
    PlayerConfig player_config;
    observations::State observations;
    growth::GrowthState growth;
    PauseState pause;
    footsteps::Config footstep_config;   // authored pool + cadence
    footsteps::State footstep_state;     // runtime cadence timer
    surfaces::Config surface_config;     // per-surface walkability (terrain collision)
    structures::Config structure_config; // resizable walk-on structures (9-slice)
    std::unordered_map<int, std::string> tile_surface;         // tile id -> surface (footsteps)
    std::unordered_map<std::size_t, std::string> cell_surface; // per-cell override (structures)
    hud::Regions hud; // fixed HUD region rects (authored layout)
    // How the player likes the game. The LIVE copy the settings screen edits; the save
    // carries it between runs (savegame::File::prefs -- it is the installation's, not any
    // pilgrim's). Authored config seeds it at boot; a save overrides that.
    settings::Settings prefs;
    HeadMarkerConfig head_marker_config;      // over-head thought-bubble feel/placement
    glimmer::Config glimmer_config;           // observable glow feel (fade + breathe)
    formulas::Config formulas;                // stat-driven formulas (Perception -> glow, etc.)
    world_items::Config world_items_config;   // world item floor-sprite feel
    interaction_mode::Config int_mode_config; // Observe/Act stance badge + SFX feel
    interaction_mode::State int_mode_state;   // stance edge-detect for the transition SFX
    watch_hud::Config watch_hud_config;       // the carried watch's readout: placement + colors
    world_config::Config world_config;        // region asset paths (map, atlas, ambient)
    worldclock::WorldClock clock;             // in-world time (notebook datelines, day/night later)
    inventory::Registry items;                // loaded item blueprints (config/items/*.json)
    loot::Registry loot_tables;               // gather loot tables (config/loot/*.json)
    crafting::Registry recipes;               // loaded recipes (config/recipes/*.json)
    crafting::Config crafting_config;         // crafting outcome/XP tuning (config/crafting.json)
    crafting::State crafting_state;           // realized-recipe discovery state
    inventory::Satchel satchel;               // what the pilgrim carries
    notebook::Record notebook; // dated record of readings (gated on carrying the notebook)
    // Unlock ids (see observations::availableUnlocks) already announced via a
    // notification, so "1 new observation / action available" toasts fire exactly
    // once per new unlock, not every frame.
    std::unordered_set<std::string> announced_unlocks;

    // Placed things this pilgrim has removed from the world for good -- a pickup taken,
    // a spot consumed -- keyed by placement id (the stable identity the map gives every
    // placement). The map is authored the same every run; this is what makes a walk's
    // changes to it stick. Spawners filter against it; anything that permanently removes
    // a placed thing records it here. See savegame::World.
    std::unordered_set<std::string> gone;

    // Where the pilgrim is in the world: the current LDtk level and its warps. Set by
    // setupRegion on every load/switch; `region` is what the save carries so a walk
    // resumes in the level it left. A warp fires only on the frame the player ENTERS
    // its box (warp_armed re-arms once outside every box), so arriving on the
    // destination's own warp -- the doormat you step out of -- never bounces back.
    std::string region;                            // current LDtk level identifier
    std::vector<ldtk::WarpPlacement> region_warps; // this level's exits
    bool region_interior = false;                  // inside space (light/sound/camera differ)
    bool warp_armed = false;
    // A warp crossed this tick, applied at the TOP of a later update -- a safe point
    // where no system holds references into the registry the switch will clear. The
    // fade below decides WHICH update: the swap happens at full black.
    struct PendingWarp
    {
        bool active = false;
        std::string level;
        std::string spawn;
    } pending_warp;
    // The fade a warp travels through: the screen darkens (Out), the region swaps at
    // full black, then the new place lightens (In). Movement is frozen while a phase
    // runs -- the step that crossed the threshold is committed. Phase length is
    // world_config.warp_fade_seconds; zero disables the fade (instant swap).
    struct WarpFade
    {
        enum class Phase
        {
            None,
            Out,
            In
        };
        Phase phase = Phase::None;
        float t = 0.0f; // seconds into the current phase
    } warp_fade;

    // Autosave bookkeeping (ephemeral -- never saved). `progress_events` counts the
    // things worth keeping (a deed enacted, a craft made, a find granted, a reading
    // landed) -- NOT the clock, which moves every frame and would make any
    // "changed?" test meaninglessly true. The loop writes when this has moved since
    // the last write and the throttle has elapsed. See progress::mark.
    unsigned progress_events = 0;
    unsigned saved_at_events = 0; // progress_events as of the last successful write
    double since_save_secs = 0.0; // world seconds since the last write (throttle)
};

// Internal pixel-art resolution. The world renders here, then INTEGER-upscales to
// the window (see gl/PixelRenderTarget). 1280x720 = 16:9; integer-fills 1440p (x2)
// and 4K (x3) exactly -- crisp, no letterbox on those. (1080p is a non-integer x1.5
// -> it letterboxes at 720p x1; acceptable, 1080p isn't the target here.) Shows 2x
// the world of the 640x360 base = a wider FOV. See docs/design/SCALE.md.
inline constexpr int kInternalWidth = 1280;
inline constexpr int kInternalHeight = 720;

// Ambient background / letterbox color. Shared by the engine clear and the
// pixel-target clear so the internal image and the letterbox bars agree.
// Placeholder muted slate -- the palette is deliberately unresolved
// (docs/design/AESTHETIC.md), so this is a neutral stand-in, not a committed
// color.
inline constexpr float kAmbientR = 0.20f;
inline constexpr float kAmbientG = 0.22f;
inline constexpr float kAmbientB = 0.24f;

// Game-side per-frame callbacks the engine invokes. The engine owns the frame
// (window, GL, fixed-step loop, clear, swap); these are where the game does its
// work. State (player entity, config) lives in the registry-context GameState.

// Bring the world into being and step into it for the pilgrim `id`: builds the region,
// spawns, overlays that pilgrim's walk (or seeds a fresh one if they've never set out),
// starts the ambient bed. Returns false if the region failed to load. Installed by main
// (which owns the region/spawn plumbing) and called by the loop when the title commits --
// the same seam the engine uses to reach the game.
using WorldEnterFn = bool (*)(Engine& engine, EntityManager& em, GameState& gs,
                              const std::string& id);
void setWorldEnter(WorldEnterFn fn);

// Swap the world to another level mid-walk (a warp crossed): tears down the region's
// entities and rebuilds from `level`, arriving at the SpawnPoint named `spawn`. The
// pilgrim's state (growth, satchel, record) lives in GameState and is untouched.
// Returns false -- and leaves the current region standing -- if the target level
// cannot be loaded. Installed by main, same seam as WorldEnterFn.
using RegionSwitchFn = bool (*)(Engine& engine, EntityManager& em, GameState& gs,
                                const std::string& level, const std::string& spawn);
void setRegionSwitch(RegionSwitchFn fn);

// Where the camera is ALLOWED to be: clamped to the map's bounds per axis, and when
// the map is smaller than the view (an interior room), pinned to its center. The one
// rule for camera placement -- the tick applies it after following the player, and
// placement applies it when snapping the camera, so the first frame after a spawn is
// already where the camera will settle (no visible slide toward the clamp).
void clampCameraToMap(EntityManager& em, const GameState& gs);

// Write the active pilgrim's walk to disk now. Reads the roster, updates only that
// pilgrim, writes it back -- so a save never clobbers anyone else's walk. No-op when
// nobody is walking. Exposed so the shutdown path can flush on the way out (the loop
// already writes on quit, page-close, and progress).
void saveNow(const EntityManager& em, GameState& gs);

void gameUpdate(Engine& engine, EntityManager& em, double dt);
void gamePreRender(Engine& engine, EntityManager& em);
void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha);
void gameRenderUI(Engine& engine, EntityManager& em);
void gameRenderImGui(Engine& engine, EntityManager& em);
