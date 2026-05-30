#pragma once

// ---------------------------------------------------------------------------
// Cinematic Scenes. A Scene is a time-bounded moment that locks player input
// while something happens -- the wake animation at game start, the Guide
// stepping out of the chapel to dispatch the beasts, a Grimoire entry
// unlocking, etc. Per docs/design/setting.md *Saves are soulslike* and
// docs/design/wood.md the game has no cutscenes in the traditional sense;
// the player keeps the camera and the world keeps rendering, but inputs
// that would commit the Vagrant to actions are blocked.
//
// One Scene at a time. Begin must be matched by end before the next
// begin. The mechanism is a Selva-side singleton (matches the existing
// pattern of selva::tuning, selva::formulas).
// ---------------------------------------------------------------------------

namespace selva::scene
{

// Input categories that a Scene can lock. Set true to BLOCK that category
// during the Scene; false leaves it free. Most Scenes lock combat+movement
// and leave mouse-look free so the player can look around while the moment
// plays out.
struct InputLock
{
    bool combat = false;   // LMB/RMB attacks, jump (F), dodge/sprint (space)
    bool movement = false; // WASD locomotion + sprint locomotion
    bool look = false;     // mouse-look camera control
};

// Start a Scene. Locks the specified input categories. Asserts no other
// Scene is currently active.
void begin(InputLock locks);

// End the current Scene. Clears all input locks. No-op if no Scene is
// active.
void end();

// True if a Scene is currently active.
bool active();

// Read-only access to current locks. Input handlers in PerFrameTick query
// this before processing presses.
const InputLock& currentLocks();

} // namespace selva::scene
