#pragma once

#include "SaveGame.h"

class Engine;
class EntityManager;

// The bridge between the running world and the document that outlives it.
// SaveGame owns the SHAPE; this owns the reading and writing of the live game
// into and out of it, which is why it is here and not there: the shape must
// stay testable without a world.
//
// There is no save verb. A job is written down at every point where something
// worth keeping changed -- a room crossed, a floor dug or climbed, a
// transaction at the staging area, a bad day, the way out -- so quitting is
// always safe and never a thing he has to remember to do.
namespace save_ops
{

// Write the world down. Cheap enough to call at every transition.
void persist(const EntityManager& em);

// Put a written-down job back and stand him in it. False when there is nothing
// to go back to, which is the shell's cue to begin one.
bool resume(Engine& engine, EntityManager& em);

// Forget the life on disk -- what "start over" would mean, kept here so the
// shell has one call for it rather than its own idea of what a save is.
void forget();

} // namespace save_ops
