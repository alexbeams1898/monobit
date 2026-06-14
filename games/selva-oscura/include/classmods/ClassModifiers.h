#pragma once

#include "AppState.h"

#include <string>

// Per-class flat modifiers on derived stat values per
// [[project_class_stats_v2_locked_2026_06_14]].
//
// Distinct concept from soft-cap curves: soft-caps SCALE the stat
// contribution; class modifiers ADD a flat value. Cosmologically,
// the modifier is what the class itself contributes -- "the burden-
// bearing soul carries more vital substance" -- separate from what
// the stat investment buys.
//
// v1 ships hp_offset only. Other modifier keys (stamina_offset,
// poise_offset, etc.) land alongside the gameplay systems that
// consume them, same incremental-author pattern as identity stats
// and soft-cap curves.
//
// Class modifiers apply only to the player. PlayerClass::None
// (enemies / pre-Signing) contributes 0 for every modifier key.

namespace selva::classmods
{

// Load per-modifier-key per-class flat modifiers from JSON. Each
// top-level key names a modifier ("hp_offset" etc.); per-class
// sub-block declares the flat int amount. Unknown classes within
// a block log + are ignored. Idempotent; safe to call on dev reload.
//
// Returns true if the file parsed and at least one modifier loaded;
// false on missing-file / parse-error (engine continues with
// last-loaded or empty config).
bool loadFromFile(const std::string& path);

// Return the flat modifier amount for `cls` under `modifier_key`.
// Returns 0 for:
//   - cls == PlayerClass::None (enemies / pre-Signing)
//   - Any (modifier_key, cls) pair without an authored entry
//   - The Unburdened with offset 0 (locked at floor by cosmology)
//
// Returned as int because flat HP / stamina / poise offsets are
// always integers; floats would invite balance-tuning noise.
int offsetFor(PlayerClass cls, const std::string& modifier_key);

} // namespace selva::classmods
