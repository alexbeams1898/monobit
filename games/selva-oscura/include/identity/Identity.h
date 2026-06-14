#pragma once

#include "AppState.h"

#include <string>

// Per-class identity stats per [[project_identity_stats_derived_erasure_locked_2026_06_14]].
//
// Identity stats are NOT stored on PlayerProfile. They are pure
// functions evaluated at read time over PlayerProfile's permanent
// action-counter ledger. One function per class lives in
// config/balance/identity_functions.json and is loaded once at boot.
//
// Mental model:
//   ledger = PlayerProfile state (flags, kill_counts, examine_counts,
//            sangue_lifetime, sangue_riversato, ...)
//   penance(profile)  = sum(f_penance over ledger)
//   anathema(profile) = sum(f_anathema over ledger)
//   ...
//
// The HUD reads computeIdentityStat(profile, profile.player_class)
// once per frame; the displayed label tier-promotes via the language
// map per the class's locked tier-promotion table:
//
//   Penitent:   Vitality -> Burden     -> Penance
//   Heretic:    Doubt    -> Unorthodoxy -> Anathema
//   Ferine:     Appetite -> Voracity   -> Predation
//   Unburdened: Resistance -> Hollowing -> Diaphany

namespace selva::identity
{

// Load per-class identity functions from JSON. Each top-level key is
// a class name ("penitent" / "heretic" / "ferine" / "unburdened").
// Each value is an input -> weight map. Unknown input kinds log a
// warning and contribute 0. Idempotent; safe to call on dev reload.
//
// Returns true if the file parsed and at least one class function
// loaded; false on missing-file / parse-error (engine continues with
// last-loaded or empty config).
bool loadFromFile(const std::string& path);

// Evaluate the identity-stat function for `cls` against the profile's
// permanent action ledger. Pure: same inputs always produce same
// output. Returns 0 for PlayerClass::None (no class yet committed)
// or when no function is loaded for the class.
//
// Per the locked doctrine the result is an integer stat value; the
// computation accumulates floats internally and floors to int at the
// boundary so weighted weights of 0.1 contribute fractionally over
// many counter ticks.
int computeIdentityStat(const PlayerProfile& profile, PlayerClass cls);

} // namespace selva::identity
