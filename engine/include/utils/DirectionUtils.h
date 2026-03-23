#pragma once

#include "ecs/Components.h"

namespace engine::direction
{

struct DirMapping
{
    int column;
    bool flip;
};

// Snap to nearest of 8 directions. No hysteresis -- for digital WASD input.
CardinalDir snapToOctant(float dx, float dy);

// Snap to nearest of 8 directions with hysteresis to prevent jitter.
CardinalDir snapWithHysteresis8(float dx, float dy, CardinalDir current);

// Map a CardinalDir to (column_index, flip_x) based on direction_count.
DirMapping dirToColumnIndex(CardinalDir dir, int direction_count, bool unique_diagonals);

// Dispatch: picks 4-way or 8-way snapping based on direction_count.
CardinalDir snapMovement(float dx, float dy, int direction_count);
CardinalDir snapFacing(float dx, float dy, CardinalDir current, int direction_count);

} // namespace engine::direction
