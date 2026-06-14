#pragma once

#include "AppState.h"

#include <string>

// Per-class soft-cap curves on derived stat values per
// [[project_class_stats_v2_locked_2026_06_14]].
//
// The doctrine: install cost is universal across classes; what
// differs is the RETURN per point on derived values (HP from END,
// stamina from END, weapon damage from STR/DEX, etc). A class that
// is "good at" a stat has its return-curve bend LATE (high cap, big
// returns deep into investment); "bad at" bends EARLY (low cap,
// fast diminishing returns).
//
// v1 ships single-bend curves: { bend_at: int, post_bend_multiplier:
// float }. Below bend_at: full returns (multiplier 1.0). At or above:
// returns scaled by post_bend_multiplier. Multi-segment curves can
// be added later by growing the JSON schema; the helper API stays
// stable.
//
// Soft-cap is PLAYER-ONLY. Enemies use the raw engine formulas; their
// HP / stamina / etc are tuned via archetype overrides, not class
// curves. Pass PlayerClass::None to apply() to bypass the soft-cap
// (returns the raw stat input unchanged).

namespace selva::softcaps
{

// Load per-derived-value per-class curves from JSON. Each top-level
// key names a derived value ("hp_from_end", "stamina_from_end",
// etc.); each per-class sub-block declares the single-bend curve.
// Unknown classes within a block log + are ignored. Idempotent;
// safe to call on dev reload.
//
// Returns true if the file parsed and at least one curve loaded;
// false on missing-file / parse-error (engine continues with
// last-loaded or empty config).
bool loadFromFile(const std::string& path);

// Apply the soft-cap for `derived_key` and `cls` to a stat value.
// The stat value is the INPUT to the engine formula (e.g. END
// before HP is computed from it). Below bend: returns stat unchanged.
// At or above bend: returns bend + (stat - bend) * post_bend_multiplier.
//
// Effect on the downstream formula: a stat of END=50 with a Penitent
// bend at 40 + multiplier 0.35 returns an effective END of
// 40 + 10*0.35 = 43.5; the HP formula then computes from 43.5
// instead of 50. The engine formula remains class-agnostic.
//
// Returns the input unchanged when:
//   - cls == PlayerClass::None (used for enemies / pre-Signing)
//   - The (derived_key, cls) pair has no curve loaded
//   - The curve has post_bend_multiplier >= 1.0 (no-op cap)
float apply(float stat_value, PlayerClass cls, const std::string& derived_key);

} // namespace selva::softcaps
