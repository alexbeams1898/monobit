#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// MovementSystem -- translates PlayerActions intent into Velocity, then integrates
// Velocity into Transform each fixed timestep.
//
// Two separate passes:
//   Pass 1 (PlayerActions -> Velocity): player-controlled entities only.
//   Pass 2 (Velocity -> Transform): all moving entities (player + AI).
//
// Called from the game update callback at the locked 60 Hz timestep.
// ---------------------------------------------------------------------------

class MovementSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
