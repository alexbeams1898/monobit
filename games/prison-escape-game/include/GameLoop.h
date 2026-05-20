#pragma once

#include "FontManager.h"

class Engine;
class EntityManager;

// One-time setup. Pass the title font so the loading overlay can draw text.
void gameLoopInit(FontHandle titleFont);

// Game-side update function. Registered with Engine::setGameUpdate() so the
// engine calls it once per fixed-step tick without knowing any game logic.
void gameUpdate(Engine& engine, EntityManager& em, double dt);

// Per-frame callback. Runs once per render frame after SDL event polling,
// before the fixed-step loop. Handles mouse-aim facing at display rate.
void gamePerFrame(Engine& engine, EntityManager& em, double dt);

// Pre-render callback. Runs after the tick loop with final render_alpha.
// Advances sprite animations at wall-clock rate, updates positions that
// must match render interpolation (e.g. crosshair on lock-on target).
void gamePreRender(Engine& engine, EntityManager& em);

// World render callback. Called once per frame between framebuffer clear
// and the UI pass. Draws the tilemap and all sprite entities for this 2D
// game. camX/camY are the engine-interpolated camera position.
void gameRenderWorld(Engine& engine, EntityManager& em, float camX, float camY, float alpha);

// Window resize callback. Resizes the game's render targets (offscreen FBOs,
// etc.) when the engine reports a new window size.
void gameOnResize(Engine& engine, int new_w, int new_h);

// Debug render callback. Called once per frame between UIRenderer::beginFrame()
// and the UI render callback. DebugDraw::setCamera() is already configured.
void gameRenderDebug(Engine& engine, EntityManager& em);

// UI render callback. Called once per frame between UIRenderer::beginFrame()
// and endFrame(). Draws HUD, menus, notifications, interaction prompts.
void gameRenderUI(Engine& engine, EntityManager& em);

// Play main menu music with random rare variant chance.
void playMainMenuMusic(EntityManager& em);
