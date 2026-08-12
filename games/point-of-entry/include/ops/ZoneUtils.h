#pragma once

class EntityManager;

// THE STATE OF THE WORK. Two states, and everything that looks different
// between them asks this and only this: is he EXTERMINATING right now, or is
// he a man carrying equipment?
//
// Resolved once a tick from the world, then read with no arguments -- so the
// trigger, the kit, the reticle and the cursor cannot answer the question
// differently within one frame, and a new visual distinction is one read
// rather than one more copy of the rule.
namespace zone
{

// Work it out for this tick. Call once, before anything reads it. `cut` is
// whether a curtain is down: the state still settles under one -- a fact about
// the room must never lag the room -- but the CHANGEOVER is held back until
// there is something to see, so it plays for him instead of behind the black.
void update(const EntityManager& em, float dt, bool cut);

// ONE RULE: can anything still reach him? Something of the swarm still on its
// feet, a hole this floor has not finished pressing, or a way down that is
// leaking -- and WHERE he happens to be standing is not part of it. A dug
// floor with every hole spent and nothing left alive is as quiet as the bar.
// The weapon fires while this is true and nowhere else, and the cursor is a
// crosshair while this is true and nowhere else.
bool combat();

// How far through the changeover the look is: 0 the instant the state flips,
// 1 once it has fully arrived. Everything that differs between the two states
// crosses over on this one number, so the whole shell turns together.
float settle();

// He is not in the world at all (the title, a fresh boot). Leaves the state
// where a stale answer cannot outlive the job it belonged to.
void reset();

} // namespace zone
