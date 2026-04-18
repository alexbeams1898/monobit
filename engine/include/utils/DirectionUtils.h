#pragma once

#include "ecs/Components.h"

namespace engine::direction
{

struct DirMapping
{
    int column;
    bool flip;
};

// Map a CardinalDir to (column_index, flip_x) based on direction_count.
// direction_count=1 -> always column 0 (static / omnidirectional).
// direction_count=4 -> S=0, W=1, E=2, N=3, flip always false.
DirMapping dirToColumnIndex(CardinalDir dir, int direction_count);

// 4-dir snap of a movement vector (used for entities without FacingDirection).
// direction_count=1 is valid (returns current dir unchanged).
CardinalDir snapMovement(float dx, float dy, int direction_count);

// 4-dir snap of a facing vector with hysteresis to prevent jitter at diagonals.
// direction_count=1 returns current unchanged.
CardinalDir snapFacing(float dx, float dy, CardinalDir current, int direction_count);

} // namespace engine::direction
