#pragma once

#include "Growth.h"
#include "Observations.h"
#include "PlayerConfig.h"

#include <vector>

#include <entt/entt.hpp>

class Engine;
class EntityManager;

// The pause page: the game's one on-demand screen, opened with F. It IS the
// growth/observation record -- there is no persistent HUD (see
// docs/design/GAME-SYSTEMS.md). While open the world update freezes and the
// soundtrack is muffled. Three tabs: self (Spirit + faculties), noticed
// (observations + conclusions), and system (a Controls sub-view + Quit; later
// settings/save). Closing is F -- there is no "Resume" item.
struct PauseState
{
    // System is the last tab (its items open sub-views / exit); the page opens
    // on Self.
    enum class Tab
    {
        Self,
        Noticed,
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
};

// Internal pixel-art resolution. The world renders here, then integer-upscales
// to the window (see gl/PixelRenderTarget). 768x432 = 16:9, 24x13.5 tiles at
// 32px; x2.5 = 1920x1080 exactly. See docs/design/SCALE.md.
inline constexpr int kInternalWidth = 768;
inline constexpr int kInternalHeight = 432;

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
