#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// SteeringSystem -- wall-repulsion and crowd-separation for NavAgent entities.
// Deflects velocity away from walls/neighbors, renormalizes to original speed.
// Forces are exponentially smoothed over time (dt) via NavAgent.smooth_steer
// so convergence is framerate-independent and flicker-free.
//
// Runs AFTER ChaseSystem, BEFORE MovementSystem.
// Tuning: REPULSION_RADIUS, REPULSION_STRENGTH, STEER_BLEND_RATE in .cpp.
// ---------------------------------------------------------------------------

class SteeringSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
