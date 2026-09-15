#pragma once

// Tiered language map: every player-visible string resolves through here, so
// one key can read differently as the player's understanding grows. Authored
// in config/lang/*.json, referenced in code by stable key and never by literal
// text. See docs/design/insight_revelation_system.md.
//
// An entry carries up to three tiers. The lowest is required and sensory; the
// higher ones are optional and resolve only once their unlock has fired. Some
// entries are deliberately authored with no top tier, which is how a name that
// must never be spoken stays unspoken.

#include <string>

namespace selva::lang
{

// Load config/lang/*.json into memory. Once at boot, before anything resolves
// a key; idempotent, clearing and reloading.
//
// A key claimed by two files is fatal -- there is no correct way to pick one.
// An entry missing its lowest tier is not: it logs, and resolves to a visible
// marker so the author meets the gap in-game rather than in a log nobody read.
void loadDirectory(const std::string& dir_path = "config/lang");

// Highest unlocked tier for a key, falling back down to the lowest. An unknown
// key resolves to "[lang:KEY]" -- missing text should be conspicuous, not an
// empty string that reads as intentional.
//
// The reference belongs to the module's map. Do not hold it across a
// loadDirectory() call; the strings move.
const std::string& resolve(const std::string& key);

// The single gate every tier promotion passes through, so the unlock graph has
// exactly one place to be consulted from.
bool isUnlocked(const std::string& node_id);

// Drop the map. Test seam and dev reload path; resolves return missing-key
// markers until loadDirectory() runs again.
void reset();

} // namespace selva::lang
