// Sangue currency operations. Pure functions over PlayerProfile fields
// so they can be unit-tested without an engine context.
//
// Cosmology: sangue is
// the Hell-substance; the Vagrant's vessel (Crucible for class-pickers,
// Censer for unburdened) holds uncommitted substance until commitment
// (installation or riversamento). Lifetime sangue is the cumulative
// counter that survives every cycle. Both are bounded by
// SANGUE_LIFETIME_CAP (9^9 = 999,999,999 -- numerologically "all of
// Hell, completed"). No player-facing string ever uses the word
// "sangue"; this header,
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

// Result of a commit attempt. Returned to the UI / log so the caller
// can show appropriate feedback (sound, animation, message).
struct CommitResult
{
    std::uint32_t amount = 0u;                 // sangue moved out of the vessel this commit
    bool fired = false;                        // true if the commit verb actually executed
    PlayerClass fire_kind = PlayerClass::None; // which fire (None = no commit; profile lacks class)
};

// Invoke the commit-fire for the active profile. Class-pickers
// (Penitent / Heretic / Wretched) fire the Crucible: vessel contents
// install into substrate (substrate effect TBD; this commit logs the
// kind and zeroes the vessel until stat-installation lands). Unburdened
// fires the Censer: vessel contents transit to Beatrice's reservoir
// (sangue_riversato accumulates). No-op for None (pre-Beat-4 -- no
// commit verb yet) and for empty vessels.
CommitResult commitVessel(PlayerProfile& p);

} // namespace selva::sangue
