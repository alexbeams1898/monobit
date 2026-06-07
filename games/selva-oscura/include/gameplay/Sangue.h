// Sangue currency operations. Pure functions over PlayerProfile fields
// so they can be unit-tested without an engine context.
//
// Cosmology: per [[project_crucible_censer_leveling_system]], sangue is
// the Hell-substance; the Vagrant's vessel (Crucible for class-pickers,
// Censer for unburdened) holds uncommitted substance until commitment
// (installation or riversamento). Lifetime sangue is the cumulative
// counter that survives every cycle. Both are bounded by
// SANGUE_LIFETIME_CAP (9^9 = 999,999,999 -- numerologically "all of
// Hell, completed"). Per [[project_substance_has_no_in_game_name]] no
// player-facing string ever uses the word "sangue"; this header,
// variable names, and code comments use it freely as the authoring
// vocabulary.

#pragma once

#include "AppState.h"

#include <cstdint>

namespace selva::sangue
{

// Grant sangue from a kill (or any future world-source). Adds to both
// the per-cycle vessel and the lifetime ledger, saturating at
// SANGUE_LIFETIME_CAP on each. Returns the actual amount granted to the
// vessel (== amount if uncapped, or the room remaining at the cap).
//
// Saturation, not wrap: a grant that would overflow the cap stops at
// the cap; subsequent grants no-op. Lifetime and vessel cap
// independently -- the vessel can be full while lifetime still has
// room, and vice versa.
std::uint32_t grantOnKill(PlayerProfile& p, std::uint32_t amount);

// Zero the vessel. Called on second death (uncommitted contents
// return to Hell, per setting.md *Hell reclaims its substance from the
// dead*). Does NOT touch lifetime -- the ledger remembers what was
// ever earned even when the vessel returns it.
void reclaimVessel(PlayerProfile& p);

} // namespace selva::sangue
