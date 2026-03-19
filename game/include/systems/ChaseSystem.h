#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// ChaseSystem — steers AI-controlled entities using the flow field.
//
// Scales to 1000+ enemies at O(1) per entity per frame:
//   1. FlowFieldSystem runs first and performs a BFS from the player, storing
//      a normalized direction in each grid cell. That BFS costs O(cells) and
//      runs only when the player enters a new 32 px cell (~6 times/sec max).
//   2. ChaseSystem does an O(1) grid lookup per enemy — no per-frame
//      pathfinding, no per-entity indirection, just a table read + multiply.
//   3. Because the flow field routes around wall cells, enemies navigate
//      around obstacles without any explicit steering logic.
//
// Must run AFTER FlowFieldSystem and BEFORE MovementSystem in Engine::update().
// ---------------------------------------------------------------------------

class ChaseSystem
{
  public:
    static void update(EntityManager& em, double dt);
};