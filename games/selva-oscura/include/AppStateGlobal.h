#pragma once

#include "AppState.h"
#include "ecs/GameComponents.h"

// ---------------------------------------------------------------------------
// Process-wide state singletons.
//
// Selva uses a single process / single save / single active character at any
// time. Rather than thread these through every system, expose them as
// free-function accessors (same pattern as selva::anim::clips(),
// selva::gameplay::archetypes(), selva::combat::equipment()).
//
// gameState() owns the current Phase, world-initialized flag, active
// character name, and pause-menu UIState. saveData() is the persisted
// SaveData currently loaded from disk; main.cpp loads it at startup via
// SaveManager::load() and writes it back on changes. playerInventory() /
// playerEquipment() hold the player's bag and equipped slots; v1 leaves
// them empty (no item content yet) but the pause menu reads them so the
// Inventory and Equipment tabs show real state once items ship.
// ---------------------------------------------------------------------------

namespace selva
{

// Mutable singleton accessor. Returns a reference to the process-wide
// GameState. Phase defaults to MainMenu.
GameState& gameState();

// Mutable singleton accessor. Returns a reference to the in-memory SaveData
// (loaded from disk at startup).
SaveData& saveData();

// Mutable singleton accessor. Returns a reference to the in-game UI overlay
// state (pause menu open/closed, active tab).
UIState& uiState();

// Mutable singleton accessor. The player's inventory (bag of items).
Inventory& playerInventory();

// Mutable singleton accessor. The player's equipped slots (right hand, left
// hand, armor pieces, accessories). Each value is an index into
// playerInventory().items, or -1 for empty.
Equipment& playerEquipment();

} // namespace selva
