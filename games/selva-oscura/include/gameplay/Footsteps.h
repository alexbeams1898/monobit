#pragma once

namespace selva::gameplay
{

struct Actor;

// Fires a footstep when a foot bone's world Y crosses into contact while it is
// descending. Read off the sampled pose rather than a per-clip cadence table,
// so it works on any clip at all -- attack windups and idle sway plant feet
// too, and a new clip needs no entry anywhere.
//
// Release uses a larger epsilon than contact: equal thresholds flicker while a
// foot rests exactly at ground level. A per-foot cooldown debounces the rest.
//
// Descent speed at the moment of the plant chooses the bank, so a shuffle is
// quiet and a stride is not, without gameplay having to declare which is
// happening.
void tickFootsteps(Actor& actor, float dt);

} // namespace selva::gameplay
