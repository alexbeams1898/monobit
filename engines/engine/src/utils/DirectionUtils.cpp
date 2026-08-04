#include "utils/DirectionUtils.h"

#include <cmath>

namespace engine::direction
{

// 4-dir snap: dominant axis wins, ties prefer vertical.
static CardinalDir snapToCardinal4(float dx, float dy)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    if (ay >= ax)
        return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
    return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
}

// Hysteresis: keep the current axis (horizontal or vertical) unless the
// other axis clearly dominates. Prevents one-frame flipping at diagonals.
static constexpr float HYSTERESIS_RATIO_4 = 0.15f;

static CardinalDir snapWithHysteresis4(float dx, float dy, CardinalDir current)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    const bool currentIsHorizontal = (current == CardinalDir::East || current == CardinalDir::West);

    if (currentIsHorizontal)
    {
        if (ay > ax + ax * HYSTERESIS_RATIO_4)
            return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    }
    if (ax > ay + ay * HYSTERESIS_RATIO_4)
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
}

DirMapping dirToColumnIndex(CardinalDir dir, int direction_count)
{
    if (direction_count <= 1)
        return {0, false};

    // 2-way: one drawing, mirrored. Art is authored facing East, so West is the flip. A sheet
    // like this has no North or South pose at all -- the direction is carried entirely by the
    // mirror, and vertical facing simply is not representable, so it never reaches here (see
    // snapFacing, which holds the current direction rather than snapping to N/S).
    if (direction_count == 2)
        return {0, dir == CardinalDir::West};

    // 4-way: S=0, W=1, E=2, N=3. No flipping.
    // clang-format off
    static constexpr int MAP4[4] = {
        0, // South
        1, // West
        2, // East
        3, // North
    };
    // clang-format on
    return {MAP4[static_cast<int>(dir)], false};
}

// A mirrored sheet has only an East and a West pose, so vertical movement carries no facing
// information: keep the side already faced rather than inventing one. This is what stops a
// character snapping to a default the moment he walks straight up.
static CardinalDir snapHorizontal(float dx, CardinalDir current)
{
    if (dx > 0.0f)
        return CardinalDir::East;
    if (dx < 0.0f)
        return CardinalDir::West;
    return current;
}

CardinalDir snapMovement(float dx, float dy, int direction_count)
{
    if (direction_count <= 1)
        return CardinalDir::South;
    if (direction_count == 2)
        return snapHorizontal(dx, CardinalDir::East);
    return snapToCardinal4(dx, dy);
}

CardinalDir snapFacing(float dx, float dy, CardinalDir current, int direction_count)
{
    if (direction_count <= 1)
        return current;
    if (direction_count == 2)
        return snapHorizontal(dx, current);
    return snapWithHysteresis4(dx, dy, current);
}

} // namespace engine::direction
