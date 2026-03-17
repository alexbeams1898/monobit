#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// AggroSystem — transitions AI entities from Idle to Chase when the player
// enters their aggro radius.
//
// Only acts on entities whose AIController has aggro_radius > 0 and are
// currently in the Idle state.  Once an entity enters Chase it stays there —
// de-aggro (Chase → Idle) is not implemented yet.
//
// Runs once per fixed-timestep update, before FlowFieldSystem, so that newly
// aggroed enemies have a valid flow-field direction on the same frame they
// enter Chase.
// ---------------------------------------------------------------------------

class AggroSystem
{
  public:
    static void update(EntityManager& em);
};
