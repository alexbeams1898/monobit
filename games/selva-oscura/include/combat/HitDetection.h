#pragma once

#include "combat/HitVolumes.h"

#include <glm/vec3.hpp>

#include <vector>

namespace selva::combat
{

// Event produced when a hitbox-vs-hurtbox overlap is detected and
// passes faction filtering + already-hit-this-swing filtering.
//
// This is the CONTRACT between the combat detection layer and
// every consumer (damage application, hit-reactions, VFX, audio,
// screen shake, freeze frames). The shape is stable — consumers
// can be rewritten freely without touching the detection layer.
struct HitEvent
{
    OwnerRef attacker;
    OwnerRef target;
    HurtRegion region = HurtRegion::Torso;
    int raw_damage = 0;
    int poise_damage = 0;
    glm::vec3 world_pos{0.0f};    // approximate point of contact
    glm::vec3 world_normal{0.0f}; // from attacker toward target, xz only
};

// Run the per-frame detection sweep. For each active hitbox,
// test against every hostile hurtbox. On overlap (passing
// faction + once-per-swing filters), emit a HitEvent.
//
// Called once per frame from gameplay AFTER both animation pose
// updates (so bone-parented volumes have current positions) and
// AFTER any clip-event-driven spawn calls (so this-frame hitboxes
// participate). Damage application + reactions are downstream
// consumers of the returned events — pure handlers, no state.
const std::vector<HitEvent>& detectHits();

// Reset the once-per-swing memo. Call when a new swing starts
// (or a new chain link) so prior hits don't block the next.
void resetHitMemo();

} // namespace selva::combat
