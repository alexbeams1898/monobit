#pragma once

#include "combat/PlayerEquipment.h"

namespace selva::combat
{

// Per-press modifier flags. Combined with the press button to pick
// which clip fires for the equipped weapon.
struct PressModifiers
{
    bool shift = false;
    bool sprinting = false;
};

// Resolve the clip name to fire for (button, modifiers) on the
// equipped weapon's class. Chain-aware: if the press lands inside
// the cancel window AND the press button matches the next slot of
// the technique the observer is currently tracking, fires that
// technique's slot N clip (the finisher path). Otherwise falls back
// to cold-strike (LMB = light[0].attacks[0], RMB = first technique
// whose slot-0 wants RMB, etc.).
//
// `wall_clock_seconds`, `cancel_window_open_at`, `cancel_window_close_at`
// gate the technique branch. Pass 0 / 0 / 0 for an unconditional
// cold-strike resolution.
//
// `out_is_chain_advance` (optional) is set to true when the returned
// clip is the next slot of an in-progress technique (i.e. a chain
// step). Set to false for cold-strike fallbacks. Lets the caller
// decide whether to cancel an in-flight one-shot — a chain advance
// inside the cancel window is intended to interrupt; an unrelated
// press should not.
//
// Returns nullptr if no clip can be resolved (no weapon, no class,
// no matching mapping). Caller treats nullptr as "press dropped."
const char* clipForButton(const PlayerEquipment& eq, HandSide hand, const char* button,
                          const PressModifiers& mods, float wall_clock_seconds,
                          float cancel_window_open_at, float cancel_window_close_at,
                          bool* out_is_chain_advance = nullptr);

} // namespace selva::combat
