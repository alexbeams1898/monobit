#pragma once

namespace selva::gameplay
{

struct Actor;

// Per-foot ground-contact tracker. Detects foot plants by sampling the
// foot bone's world Y from the actor's PoseSampler each frame; fires an
// SFX when a foot transitions from Airborne to Grounded. Works on ANY
// clip (locomotion, attack windup, get-up, idle micro-sway) because the
// signal comes from the visible foot, not from a per-clip cadence
// table.
//
// State machine per foot:
//   Airborne -> Grounded  when foot.y <= ground+contact_epsilon AND
//                         the foot was descending the prior frame.
//                         Fires playSfx on entry. Per-foot cooldown
//                         (~120ms) hard-debounces.
//   Grounded -> Airborne  when foot.y > ground+release_epsilon.
//                         Hysteresis (release > contact) prevents
//                         flicker when the foot sits exactly at the
//                         threshold.
//
// SFX bank pick (walk vs run) is keyed off the foot's descent velocity
// at the moment of plant: a soft plant (idle sway, attack windup foot
// shuffle) plays the walk bank; a hard plant (running stride) plays
// the run bank. This frees us from gameplay flags like `sprinting` —
// the visible foot tells us what's actually happening.
//
// Per-actor state lives on Actor (added as a FootstepState member).
// This module owns the tick logic only; it reads foot positions out of
// actor.sampler and writes nothing back to the actor.
void tickFootsteps(Actor& actor, float dt);

} // namespace selva::gameplay
