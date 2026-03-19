#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// SteeringSystem — wall-repulsion steering for AI entities.
//
// Problem solved:
//   The flow field routes enemies through "routable" cells (cells at least
//   one cell-width away from any wall). The continuous AABB physics means
//   enemies can drift laterally into "clearance zones" (cells adjacent to
//   walls) while following the flow field. Once there, MovementSystem's axis-
//   projection zeros their velocity against the wall surface and they freeze.
//
// Solution:
//   Each frame, for every chasing AI entity, accumulate a repulsion force
//   from all static walls within REPULSION_RADIUS world-units of the entity
//   center. Blend that force into the entity's velocity, then renormalize to
//   the original speed. Net effect: direction changes (deflects away from
//   walls), speed is preserved.
//
// Must run AFTER ChaseSystem (velocity set by flow field)
//        and BEFORE MovementSystem (velocity applied to transform).
//
// Tuning constants are in SteeringSystem.cpp:
//   REPULSION_RADIUS   — distance (px from wall surface) at which force starts
//   REPULSION_STRENGTH — how aggressively the force deflects the velocity
// ---------------------------------------------------------------------------

class SteeringSystem
{
  public:
    static void update(EntityManager& em);
};
