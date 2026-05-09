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
// equipped weapon's class. Reads the class's presses table if
// present; otherwise derives from the technique config (LMB =
// light[0].attacks[0], RMB = first technique whose slot-0 wants RMB,
// fall back to light[0]).
//
// Returns nullptr if no clip can be resolved (no weapon, no class,
// no matching mapping). Caller treats nullptr as "press dropped."
const char* clipForButton(const PlayerEquipment& eq, HandSide hand, const char* button,
                          const PressModifiers& mods);

} // namespace selva::combat
