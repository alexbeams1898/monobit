#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// FlowFieldSystem -- builds a BFS navigation map toward a target position.
// Generic navigation infrastructure: any entity with NavAgent + Transform +
// Velocity is counted in the density grid.
//
// Must run BEFORE ChaseSystem so that the field is current when velocities
// are written.
// ---------------------------------------------------------------------------

class FlowFieldSystem
{
  public:
    static void update(EntityManager& em, float targetX, float targetY);
};
