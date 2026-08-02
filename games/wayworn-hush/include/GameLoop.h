#pragma once

#include "Ambience.h"
#include "AppState.h"
#include "Arcs.h"
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
#include "Npc.h"
#include "PlayerConfig.h"
#include "Psyche.h"
#include "Scene.h"
#include "Settings.h"
#include "SpiritHud.h"
#include "Structures.h"
#include "Surfaces.h"
#include "Tutorial.h"
#include "WatchHud.h"
#include "WorldClock.h"
#include "WorldConfig.h"
#include "WorldItems.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>

class Engine;
class EntityManager;

// The pause page: the game's one on-demand screen, opened with F. It IS the
// growth/observation record -- the DEEP record, as against the HUD, which carries
// only what is always true at a glance (see docs/design/HUD.md and settings::Hud).
// While open the world update freezes and the
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
    psyche::State psyche;
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
    spirit_hud::Config spirit_hud_config;     // the Spirit total + its "+N" feed
    world_config::Config world_config;        // region asset paths (map, atlas, ambient)
    worldclock::WorldClock clock;             // in-world time (notebook datelines, day/night later)
    inventory::Registry items;                // loaded item blueprints (config/items/*.json)
    npc::Registry npcs;                       // authored characters (config/npcs/*.json)
    yields::Registry yield_tables;            // gather yield tables (config/yields/*.json)
    crafting::Registry recipes;               // loaded recipes (config/recipes/*.json)
    crafting::Config crafting_config;         // crafting outcome/XP tuning (config/crafting.json)
    crafting::State crafting_state;           // realized-recipe discovery state
    inventory::Satchel satchel;               // what the pilgrim carries
    notebook::Record notebook; // dated record of readings (gated on carrying the notebook)
    // Authored threads (config/arcs.json). Their ROUTES are checked at boot and never read
    // again; the arcs that carry a written line surface as the notebook's agenda. Named for
    // the concept rather than the file so it cannot shadow the `arcs::` namespace here.
    arcs::Registry threads;
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
    // Every warp id in the map -> the level holding it (ldtk::warpIndex, built once
    // at boot). A door names the door it arrives at; this is what turns that name
    // into a level, so the map never states the level a second time.
    std::unordered_map<std::string, std::string> warp_levels;
    std::vector<ldtk::SpawnPoint> region_spawns; // named points (arrivals + scene marks)
    // A CLEARING: several placed things that together mean one change to the world (the
    // piles across a path). Group name -> every placement in it, and the flag its emptying
    // raises. Rebuilt from the authored map on every region switch, so it counts what the
    // map says exists; `gone` says which of them this pilgrim has already taken.
    struct Clearing
    {
        std::vector<std::string> placements;
        std::string flag;
    };
    std::unordered_map<std::string, Clearing> region_clearings;
    // The level's PLACED characters by npc id, so a scene can steer someone who is
    // already standing there (no `enter` needed) -- scene-spawned bodies shadow these.
    std::unordered_map<std::string, entt::entity> region_npcs;
    // Each placed npc's ambient-routine progress (see npc::RoutineStep and the
    // routine tick). Keyed like region_npcs and rebuilt with it on every region
    // switch -- entities don't survive the switch, so neither does this.
    struct NpcRoutineState
    {
        std::string routine; // the schedule entry in force (a switch resets progress)
        std::size_t step = 0;
        float wait_left = -1.0f; // <0 = the current Wait hasn't drawn its duration yet
        bool moving = false;
        float tx = 0.0f, ty = 0.0f;         // current walk target
        float home_x = 0.0f, home_y = 0.0f; // placed spot -- wander anchors here
        std::uint32_t rng = 0;              // per-npc stream (wander spots, wait draws)
    };
    std::unordered_map<std::string, NpcRoutineState> npc_routines;
    bool region_interior = false; // inside space (light/sound/camera differ)
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

    scene::Registry scenes;           // authored scenes (config/scenes/*.json), loaded at boot
    scene::Runtime scene_rt;          // the running scene, if any -- movement holds while active
    ambience::Config ambience_config; // named world-sound channels (config/ambience.json)
    ambience::State ambience_state;   // which channels are sounding
    tutorial::Config tutorial_config; // first-time teaching cards (config/tutorial.json)
    tutorial::State tutorial_state;   // seen set (saved) + the queued card, if any

    // Whether the ambient bed (the score) is sounding yet this walk. The world may
    // hold it back behind world_config.ambient_gate_flag -- the opening belongs to
    // the room's own sounds; the score enters when that flag lands (or immediately,
    // on walks that already hold it). Ephemeral; set at world-enter.
    bool music_started = false;

    // The walk metronome's last-seen player position (ephemeral). Invalid until
    // anchored; a jump larger than any honest stride (a warp, a spawn) re-anchors
    // without billing the hour -- teleporting is not hiking.
    float clock_px = 0.0f;
    float clock_py = 0.0f;
    bool clock_pos_valid = false;

    // --- EDGE DETECTORS ------------------------------------------------------------------
    // Each field below is a "what it looked like last time" baseline, and each pump that reads
    // one treats a difference as an EVENT. That makes them all share one failure: at world
    // enter the baseline is default-constructed while the world is fully restored, so the
    // entire walk reads as having just happened -- an hour crossing, every item arriving, a
    // flag landing. The symptoms look unrelated (a TV clunk at boot, a want-line at the title,
    // thoughts re-rolling) but they are one bug.
    //
    // THE RULE: an arrival is not an event. Every baseline here is seeded from the RESTORED
    // world in the one block at world-enter (see main.cpp, "EVERY edge detector is seeded").
    // A new pump adds its baseline here AND its seeding there, in the same change -- a
    // baseline seeded nowhere is a bug that only shows up in play, on load, once.
    //
    // The stat-change pump's fingerprint: growth::levelSum as of the last engine
    // run over the stat keys. Lives HERE (per-walk, ephemeral, never saved) so resuming a
    // save can never masquerade as growth and re-roll thoughts nobody earned. -1 = unseeded.
    int stats_seen_sum = -1;
    // The last whole hour the psyche engine was re-run for (see the clock mirror in
    // gameUpdate): a crossed hour re-offers the stat keys, because content can be gated on the
    // time of day. Seeded from the restored clock, or the first tick reads as an hour passing.
    int clock_hour_seen = -1;
    // Unlock ids (see psyche::availableUnlocks) already announced via a notification, so
    // "1 new observation / action available" toasts fire exactly once per new unlock.
    std::unordered_set<std::string> announced_unlocks;
    // Spots whose baseline deeds the unlock pump has already seeded as announced (a deed
    // reachable the moment its spot is FIRST observed is what the menu shows, not news).
    // Rebuilt from the observation record by the first pump.
    std::unordered_set<std::string> seeded_spots;
    // What he carried and held as of the last mirror lives on psyche::State (carrying /
    // holding) rather than here, because the engine itself diffs against them -- but they are
    // edge-detector baselines all the same, and are seeded in the same block.

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
