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

// Debug render callback. Called once per frame between UIRenderer::beginFrame()
// and the UI render callback. DebugDraw::setCamera() is already configured.
void gameRenderDebug(Engine& engine, EntityManager& em);

// UI render callback. Called once per frame between UIRenderer::beginFrame()
// and endFrame(). Draws HUD, menus, notifications, interaction prompts.
void gameRenderUI(Engine& engine, EntityManager& em);
