#pragma once

#include "AppState.h"

class Engine;

namespace selva::ui
{

// Render the screen for the current GameState::phase, or the pause overlay
// if Playing and the pause menu is open. Called once per frame from
// selvaRenderImGui. Handles input/transitions internally - main.cpp does
// not need to know which screen is active.
//
// Returns true if the game should quit (Quit selected from main menu or
// pause menu). Caller (main.cpp) is responsible for breaking the engine
// loop on true.
bool renderScreens(Engine& engine);

// Force-load screen assets (main menu logo, etc.) at boot so the first
// screen render doesn't stall a frame with a synchronous texture load.
// Idempotent - no-op if already loaded. Called from main.cpp during
// the loading-screen phase (where a synchronous disk read + GPU
// upload is already blocking gameplay anyway).
//
// Trace 20 identified `screens-mainmenu` first-frame spikes to 31 ms
// while subsequent frames were 0.03 ms -- the whole spike was the
// lazy logo texture load fired from ensureLogoLoaded() on first render.
void preloadScreenAssets();

// Phase transition chokepoint. Per systems.md *Screens*, all phase
// changes flow through this function so phase-enter/-exit hooks
// (audio bed swaps, region activation, cursor capture mode, etc.)
// stay coherent. Use this rather than mutating gameState().phase
// directly. Safe to call from a crash-recovery context as long as
// the target phase is a "safe" one (MainMenu).
void setPhase(GameState::Phase next);

} // namespace selva::ui
