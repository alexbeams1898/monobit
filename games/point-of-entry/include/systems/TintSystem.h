#pragma once

#include "ecs/EntityManager.h"

#include <string>

// WHAT A BODY LOOKS LIKE, DERIVED. One system owns TintOverride and nothing else may write it:
// the pool is cleared every frame and the whole ladder re-decided from what is true right now.
//
// This is the point of the design rather than a detail of it. A tint that is SET when something
// happens and REMOVED when it stops has to be un-set correctly by every writer, and two writers
// cannot both own removal -- a flash clearing itself would wipe the redness underneath it, and a
// redness that has to notice it is no longer needed will one day fail to. Derived per tick, a
// stale tint cannot exist: it is either true this frame or it is gone.
//
// The ladder is a PRIORITY ORDER, most urgent first. What is being struck matters more than what
// is wounded, so a flash overrides a colour rather than mixing with it.
namespace tint
{

// Read the feedback block of the game's tuning document. Missing values keep their defaults.
bool load(const std::string& path = "config/stats.json");

// How long a flash lasts, in seconds. Asked for by whoever decides a body was struck, so the
// length of a flash and the look of one stay in one place.
float flashSeconds(bool fatal);

// Re-decide every tint, from scratch.
void update(EntityManager& em);

// Take every tint away. For when the world stops ticking with something mid-flight: the pass
// re-derives per frame and so cannot go stale, but it cannot correct what it is not running to
// correct, and a frozen white body fading out under the black is exactly that.
void forget(EntityManager& em);

} // namespace tint
