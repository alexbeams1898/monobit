#pragma once

// Tiered language map. The Vagrant's understanding of the world grows
// through play; every player-visible string in the game flows through
// this module so it can be re-resolved as insight unlocks fire.
//
// Per docs/design/insight_revelation_system.md:
//   - One data-driven language graph powers every surface (examine
//     text, interaction labels, NPC display names, item descriptions,
//     UI strings).
//   - Each language entry has up to three tiers (tier_0 sensory /
//     functional, tier_1 English gloss with operational understanding,
//     tier_2 the cosmological proper name -- "sangue", "contrapasso",
//     etc.).
//   - tier_0 is REQUIRED. tier_1 / tier_2 are optional; if absent,
//     resolve returns the highest authored tier the player has
//     unlocked.
//   - Insight node unlocks are stubbed for v1 (always returns false /
//     always-tier-0). When the unlock graph lands, this is the SINGLE
//     hook to evolve -- callers don't change.
//
// Doctrine: player-facing strings live in config/lang/*.json. Code
// references stable string KEYS (e.g. "world.acheron_pile.examine"),
// never the literal text. The substance-has-no-in-game-name pillar
// (per project_substance_has_no_in_game_name) is enforced by NOT
// authoring a tier_2 entry for the sangue substance specifically --
// no tier ever reveals its proper name.

#include <string>

namespace selva::lang
{

// Load every config/lang/*.json file into the in-memory map. Called
// once at boot, after audio + before any UI render that resolves
// keys. Idempotent if called again (clears + reloads).
//
// Validation: every entry must have a tier_0. Duplicate keys across
// files are fatal (means two domain files claimed the same key --
// authoring bug). Missing tier_0 on a single entry is logged but
// non-fatal; resolve() will return [lang:missing-tier-0:KEY] so the
// author sees the issue in-game.
void loadDirectory(const std::string& dir_path = "config/lang");

// Resolve a key to its highest-unlocked-tier text. Returns:
//   - The tier_2 text if the entry has one AND its unlock node has
//     fired.
//   - Else the tier_1 text if present AND its unlock node fired.
//   - Else the tier_0 text.
//   - If the key doesn't exist in the map: returns "[lang:KEY]" so
//     the missing-key shows up in-game as an obvious authoring bug,
//     not as a silent empty string.
//
// Returned string is owned by the lang module's in-memory map; safe
// to use as a printf %s arg or to copy. Do NOT store the pointer
// across loadDirectory() calls (the map's strings can move on reload).
const std::string& resolve(const std::string& key);

// Insight node check. Stub for v1: returns false for every node id.
// When the insight graph ships, this becomes a lookup into the active
// profile's unlocked_insights set. EVERY tier promotion goes through
// here -- one site to evolve.
bool isUnlocked(const std::string& node_id);

// Reset the in-memory map (test seam + dev reload path). Safe to call
// at any time; next resolve() call after this returns missing-key
// fallbacks until loadDirectory() runs again.
void reset();

} // namespace selva::lang
