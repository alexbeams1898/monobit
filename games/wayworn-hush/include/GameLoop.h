#pragma once

#include "Footsteps.h"
#include "Formulas.h"
#include "Glimmer.h"
#include "Growth.h"
#include "HeadMarker.h"
#include "HudCanvas.h"
#include "Inventory.h"
#include "Notebook.h"
#include "Observations.h"
#include "PlayerConfig.h"
#include "Structures.h"
#include "Surfaces.h"
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
// soundtrack is muffled. Tabs: Self (Spirit + faculties), Noticed (observations +
// conclusions), Satchel (carried items), Notebook (dated record of readings), and
// System (a Controls sub-view + Quit; later settings/save). Closing is F -- there
// is no "Resume" item.
struct PauseState
{
    // System is the last tab (its items open sub-views / exit); the page opens
    // on Self. Satchel = what you carry; Notebook = the dated record of readings.
    enum class Tab
    {
        Self,
        Noticed,
        Satchel,
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
};

// Game-wide runtime state, stored as a singleton in the entt registry context
// (reg.ctx()). Systems read it from there rather than from file-scope globals.
struct GameState
{
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
    hud::Regions hud;                       // fixed HUD region rects + visibility mode
    HeadMarkerConfig head_marker_config;    // over-head thought-bubble feel/placement
    glimmer::Config glimmer_config;         // observable glow feel (fade + breathe)
    formulas::Config formulas;              // stat-driven formulas (Perception -> glow, etc.)
    world_items::Config world_items_config; // world item floor-sprite feel
    world_config::Config world_config;      // region asset paths (map, atlas, ambient)
    worldclock::WorldClock clock;           // in-world time (notebook datelines, day/night later)
    inventory::Registry items;              // loaded item blueprints (config/items/*.json)
    loot::Registry loot_tables;             // gather loot tables (config/loot/*.json)
    inventory::Satchel satchel;             // what the pilgrim carries
    notebook::Record notebook; // dated record of readings (gated on carrying the notebook)
    // Unlock ids (see observations::availableUnlocks) already announced via a
    // notification, so "1 new observation / action available" toasts fire exactly
    // once per new unlock, not every frame.
    std::unordered_set<std::string> announced_unlocks;
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

void gameUpdate(Engine& engine, EntityManager& em, double dt);
void gamePreRender(Engine& engine, EntityManager& em);
void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha);
void gameRenderUI(Engine& engine, EntityManager& em);
void gameRenderImGui(Engine& engine, EntityManager& em);
