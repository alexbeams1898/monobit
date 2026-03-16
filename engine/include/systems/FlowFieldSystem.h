#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// FlowFieldSystem — builds a BFS navigation map from the player's position.
//
// A flow field maps every grid cell to a normalized direction vector pointing
// toward the shortest obstacle-free path to the player. ChaseSystem reads this
// instead of aiming directly at the player, so enemies navigate around walls.
//
// Performance model:
//   - BFS runs only when the player enters a new 16 px grid cell.
//   - At player speed ≈ 200 px/s, that is at most ~12 rebuilds per second.
//   - Each rebuild visits at most FlowField::COLS * FlowField::ROWS = 16384 cells.
//   - ChaseSystem then reads O(1) per enemy — no per-frame pathfinding cost.
//
// Must run BEFORE ChaseSystem so that the field is current when velocities
// are written.
// ---------------------------------------------------------------------------

class FlowFieldSystem
{
  public:
    static void update(EntityManager& em);
};
