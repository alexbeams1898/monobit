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

// Resolve the PlayerProfile for the currently-active character.
// Looks up saveData().characters by name. An UNNAMED character is
// a PlayerProfile with `name == ""` -- a real entry in saveData,
// just untitled. active_character holds that empty string for
// unnamed runs. Per the locked design (one unnamed character at a
// time): a single name=="" profile is allowed in saveData; the
// equality lookup resolves to it. Returns nullptr only if no
// profile matches (e.g. mid-MainMenu with no run loaded; defensive
// for unexpected states). Mutable -- callers may modify; the next
// save flushes mutations to disk.
PlayerProfile* activePlayerProfile();

// Return the display string for the active character's name. Names
// can be empty (a not-yet-named run); this returns "???" in that
// case so display surfaces (Status tab, Load menu, etc.) get a
// consistent placeholder without each having to handle the empty
// case. Use this for any PLAYER-FACING string; for internal lookup
// (file paths, save ids, etc.) use the raw active_character /
// PlayerProfile.name field.
std::string activeCharacterDisplayName();

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

// Insight-node helpers: read / write PlayerProfile.unlocked_insights
// with the same dedup + first-set semantics as the flag helpers.
// selva::lang::isUnlocked(node_id) calls into hasInsight() to resolve
// tier promotions; selva::insight::tick() calls setInsight() when a
// trigger fires. Convention for node ids: stable semantic identifiers
// like "knows_guide", "knows_lupa", "knows_acheron_pile". Renaming a
// node breaks both existing saves AND the language map; treat names
// like a save schema field.
bool hasInsight(const PlayerProfile* profile, const std::string& node);
bool setInsight(PlayerProfile* profile, const std::string& node);
bool clearInsight(PlayerProfile* profile, const std::string& node);
bool hasInsight(const std::string& node);
bool setInsight(const std::string& node);
bool clearInsight(const std::string& node);

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
