#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// MovementSystem — translates Input intent into Velocity, then integrates
// Velocity into Transform each fixed timestep.
//
// Two separate passes keep the concerns cleanly separated:
//   Pass 1 (Input → Velocity): only player-controlled entities are affected.
//   Pass 2 (Velocity → Transform): all moving entities benefit — including
//     future AI-controlled enemies that write directly to Velocity.
//
// Called from Engine::update(dt) at the locked 60 Hz timestep.
// ---------------------------------------------------------------------------

class MovementSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
