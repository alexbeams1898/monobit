#pragma once

namespace selva::tuning
{
struct Tunables;
}

namespace selva::gameplay
{

struct Actor;

// AI decision-tick scheduler.
//
// Per-frame work that is *event-reactive* (perception, hit detection,
// hit reactions, animation, hip-delta apply, collision) runs at the
// full render rate so it stays responsive to fast player movement.
//
// Per-frame work that is *decision-making* (behavior tree evaluation,
// action selection, locomotion-intent updates) runs at a lower fixed
// rate — `ai_decision_tick_hz` (default 10Hz). A 100ms latency between
// "enemy decided to swing" and "the swing fires" is invisible to the
// player but enormous savings vs running tree traversal every frame
// across N enemies.
//
// `shouldTickAi` is a gate function. It returns true at most
// `effective_hz` times per second per actor. When it returns true,
// the caller is expected to perform the actor's AI work, then the
// scheduler advances the actor's next tick time by `1/effective_hz`
// plus a small per-tick jitter so actors don't drift back into
// lock-step over time.
//
// `effective_hz` is `ai_decision_tick_hz` for non-combat actors and
// `ai_decision_tick_hz * ai_decision_tick_combat_hz_multiplier` for
// actors whose perception state is Awareness::Combat. Souls
// convention: alerted enemies think faster than dormant ones.
bool shouldTickAi(Actor& actor, const selva::tuning::Tunables& tun);

// Stagger an actor's first AI tick across the decision-tick window
// using its pool index as a phase. Called at spawn so a wave of
// enemies doesn't all evaluate on the same frame.
void seedAiTickPhase(Actor& actor, int pool_index, const selva::tuning::Tunables& tun);

} // namespace selva::gameplay
