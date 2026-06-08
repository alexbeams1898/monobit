#pragma once

// Per-event scripted-moment system. Bespoke "this beat plays once when
// flag X transitions" code lives here, with each event a free function
// that:
//   1. Checks its precondition (flags, state, etc.).
//   2. Calls selva::scene::begin() to lock player input categories.
//   3. Mutates world state (open a door, set an Actor.scripted_target_pos,
//      etc.) -- the world keeps simulating, the player watches.
//   4. Watches for completion (target reached, animation done, ...).
//   5. Calls selva::scene::end() and sets a "done" flag so it doesn't
//      re-fire next session.
//
// tickScriptedEvents() is called once per frame from PerFrameTick.
// Lightweight (a few flag checks). v1 shape: one C++ function per
// event. The migration target is a declarative event registry
// (data-driven flag conditions + scripted actions); deferred until
// the event count motivates the registry indirection.

namespace selva::gameplay
{

// Per-frame: scan for trigger conditions on every scripted event,
// fire the matching one if its precondition is newly satisfied,
// advance any in-flight event toward completion.
void tickScriptedEvents();

// Custom-trigger entry point. The chapel_interior trigger volume
// `guide_force_engage` calls this when the player walks past the
// Guide toward the descent stairs without having talked to him.
// Begins a Scene + sends the Guide on a scripted-walk to intercept.
// Idempotent (no-op if already mid-intercept or Signing committed).
void onForceEngageGuide();

} // namespace selva::gameplay
