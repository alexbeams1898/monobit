#pragma once

#include "PlayerConfig.h"

#include <entt/entt.hpp>

class Engine;
class EntityManager;

// Game-wide runtime state, stored as a singleton in the entt registry context
// (reg.ctx()). Systems read it from there rather than from file-scope globals.
struct GameState
{
    entt::entity player = entt::null;
    PlayerConfig player_config;
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
