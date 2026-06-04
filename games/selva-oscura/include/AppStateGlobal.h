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

// Resolve the PlayerProfile for the currently-active character (named by
// gameState().active_character). Returns nullptr if no profile matches
// (no active character, or save out of sync). Mutable -- callers may
// modify (e.g. append a felled-boss id then mark save dirty).
PlayerProfile* activePlayerProfile();

// Quest-flag helpers: read / write PlayerProfile.flags through a single
// API so dedup + "first set?" semantics live in one place.
//   hasFlag  -- true if the flag is in the set
//   setFlag  -- adds the flag if not present; returns TRUE on first set,
//               FALSE if already present (load-bearing for achievement
//               systems that fire on first-set-only)
//   clearFlag -- removes; returns TRUE if removed, FALSE if absent
// All three are no-ops + return false on a null profile (defensive --
// allows call-from-anywhere without active-character guard).
//
// Convention for flag names: stable semantic identifiers like
// "lupa_felled", "met_guide", "grimoire_5". Renaming a flag breaks
// existing saves; treat names like a save schema field.
bool hasFlag(const PlayerProfile* profile, const std::string& flag);
bool setFlag(PlayerProfile* profile, const std::string& flag);
bool clearFlag(PlayerProfile* profile, const std::string& flag);

// Convenience overloads that resolve the active profile. Same return
// semantics; no-op if no active profile.
bool hasFlag(const std::string& flag);
bool setFlag(const std::string& flag);
bool clearFlag(const std::string& flag);

// Per-NPC encounter record. Lazily creates the entry on first access
// so callers never null-check. Mutating the returned reference
// persists through the next save. nullptr profile = returns a static
// scratch entry (writes effectively no-op).
selva::dialog::NpcEncounterState& npcEncounter(PlayerProfile* profile, const std::string& npc_id);

// True if the player has seen the given topic for this NPC. Checks
// the profile's encounter state. False for unknown NPC, no active
// profile, or topic not yet seen.
bool hasSeenTopic(const PlayerProfile* profile, const std::string& npc_id,
                  const std::string& topic_id);
bool hasSeenTopic(const std::string& npc_id, const std::string& topic_id);

} // namespace selva
