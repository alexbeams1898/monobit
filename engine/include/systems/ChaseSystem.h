#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// ChaseSystem — steers AI-controlled entities toward the player each tick.
//
// Performance design (VS-style, scales to 1000+ enemies):
//   1. Player position is fetched ONCE by querying for the Input component tag.
//   2. A single loop sweeps all (AIController, Transform, Velocity) entities
//      with pure arithmetic — no per-entity lookups, no allocations.
//   3. Velocity is written; MovementSystem integrates it for free next pass.
//
// Must run BEFORE MovementSystem in Engine::update() so that written velocities
// are integrated in the same frame.
// ---------------------------------------------------------------------------

class ChaseSystem
{
  public:
    static void update(EntityManager& em);
};