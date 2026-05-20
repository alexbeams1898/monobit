#pragma once

#include <glm/vec3.hpp>

namespace selva::tuning
{
struct Tunables;
}

namespace selva::gameplay
{

struct Actor;

// What an actor's perception system currently believes about the
// world. Awareness levels are the AI's high-level state; downstream
// systems (behavior tree, locomotion intent) branch on this enum
// rather than reading sensor data directly.
//
// Transitions are driven by tickPerception each frame:
//   Unaware    -> Suspicious : saw the player this tick
//   Suspicious -> Alerted    : accumulated `confirmed_sightings_to_alert`
//                              sightings inside the suspicion window
//   Suspicious -> Unaware    : no sighting for `suspicion_decay_seconds`
//   Alerted    -> Combat     : within `combat_engage_range` of player
//   Alerted    -> Suspicious : no contact for `alerted_decay_seconds`
//   Combat     -> Alerted    : continuously outside leash range for
//                              `combat_disengage_seconds`
//
// Combat retention is DISTANCE-BASED, not vision-based. Souls
// convention: once an enemy is engaged with you, they remember you
// exist regardless of LOS — the cone determines initial engagement,
// the leash determines retention. While in Combat,
// last_known_player_pos is updated every frame from the actual
// player position; the enemy "knows" where you are even when not
// looking at you. They just can't *swing* at stale data — the
// LeafPickAction freshness gate ensures attacks fire only when
// vision is fresh.
//
// The Suspicious -> Alerted gating is what gives Souls enemies their
// "double-take" feel: the enemy notices the player, hesitates, then
// commits. Without it, every enemy snaps to full-aggro on first sight.
enum class Awareness
{
    Unaware,
    Suspicious,
    Alerted,
    Combat,
};

// Per-actor perception state. Lives on Actor; updated by tickPerception.
struct PerceptionState
{
    Awareness awareness = Awareness::Unaware;

    // Last frame the perception system confirmed the player's position
    // via vision. -1 = never seen.
    float last_seen_time = -1.0f;

    // Player's world position at last_seen_time. The behavior tree
    // reads this when investigating a lost contact.
    glm::vec3 last_known_player_pos = glm::vec3(0.0f);

    // Wallclock time the actor entered its current awareness level.
    // Used by decay timers to know "how long has nothing happened?"
    float awareness_entered_time = 0.0f;

    // Count of vision-hits inside the current Suspicious window. Hits
    // `confirmed_sightings_to_alert` -> escalate to Alerted. Reset on
    // every Unaware<->Suspicious edge.
    int suspicious_sighting_count = 0;

    // Wallclock time the player first stepped outside leash range
    // while this actor was in Combat awareness. -1 = currently inside
    // leash (or not in Combat). Drives the Combat -> Alerted decay:
    // disengage fires only after the player has been continuously
    // outside leash for `ai_combat_disengage_seconds`. Hysteresis on
    // the leash boundary prevents flicker.
    float outside_leash_since = -1.0f;
};

// Update `actor.perception` from the world's current state. Reads the
// player's world position via the supplied reference (not pulled from
// gameplay::player() so we can unit-test the function with any pair of
// Actors). Cheap: one cone-cast per call, no allocations.
void tickPerception(Actor& actor, const Actor& player, float dt,
                    const selva::tuning::Tunables& tun);

} // namespace selva::gameplay
