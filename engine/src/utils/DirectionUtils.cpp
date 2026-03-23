#include "utils/DirectionUtils.h"

#include <cmath>

namespace engine::direction
{

// tan(22.5 degrees) -- boundary between cardinal and diagonal sectors.
static constexpr float TAN_22_5 = 0.41421356f;

CardinalDir snapToOctant(float dx, float dy)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);

    if (ax < 1e-6f && ay < 1e-6f)
        return CardinalDir::South;

    // Minor axis / major axis < tan(22.5) -> cardinal, else diagonal.
    if (ay >= ax)
    {
        if (ax < ay * TAN_22_5)
            return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
        return dy > 0.0f ? (dx > 0.0f ? CardinalDir::SouthEast : CardinalDir::SouthWest)
                         : (dx > 0.0f ? CardinalDir::NorthEast : CardinalDir::NorthWest);
    }
    if (ay < ax * TAN_22_5)
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    return dy > 0.0f ? (dx > 0.0f ? CardinalDir::SouthEast : CardinalDir::SouthWest)
                     : (dx > 0.0f ? CardinalDir::NorthEast : CardinalDir::NorthWest);
}

// 4-dir snap: dominant axis wins, ties prefer vertical.
static CardinalDir snapToCardinal4(float dx, float dy)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    if (ay >= ax)
        return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
    return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
}

// 8 sector center unit vectors (clockwise from South, matching CardinalDir order).
// clang-format off
static constexpr float SECTOR_CENTERS[8][2] = {
    { 0.0f,       1.0f},       // South
    {-0.70711f,   0.70711f},   // SouthWest
    {-1.0f,       0.0f},       // West
    {-0.70711f,  -0.70711f},   // NorthWest
    { 0.0f,      -1.0f},       // North
    { 0.70711f,  -0.70711f},   // NorthEast
    { 1.0f,       0.0f},       // East
    { 0.70711f,   0.70711f},   // SouthEast
};
// clang-format on

// cos(22.5 + margin) -- threshold below which we re-evaluate direction.
static constexpr float HYSTERESIS_DOT = 0.887f;

CardinalDir snapWithHysteresis8(float dx, float dy, CardinalDir current)
{
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f)
        return current;

    const float nx = dx / len;
    const float ny = dy / len;

    const int curIdx = static_cast<int>(current);
    const float dot = nx * SECTOR_CENTERS[curIdx][0] + ny * SECTOR_CENTERS[curIdx][1];

    // Still within current sector's cone -- keep it.
    if (dot >= HYSTERESIS_DOT)
        return current;

    // Find best matching sector.
    float bestDot = -2.0f;
    int bestIdx = curIdx;
    for (int i = 0; i < 8; ++i)
    {
        const float d = nx * SECTOR_CENTERS[i][0] + ny * SECTOR_CENTERS[i][1];
        if (d > bestDot)
        {
            bestDot = d;
            bestIdx = i;
        }
    }
    return static_cast<CardinalDir>(bestIdx);
}

// 4-dir hysteresis constants.
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

DirMapping dirToColumnIndex(CardinalDir dir, int direction_count, bool unique_diagonals)
{
    const int idx = static_cast<int>(dir);

    if (direction_count <= 1)
        return {0, false};

    if (direction_count == 4)
    {
        // S=0, SW->S=0, W=1, NW->W=1, N=3, NE->N=3, E=2, SE->E=2
        // clang-format off
        static constexpr int MAP4[8] = {0, 0, 1, 1, 3, 3, 2, 2};
        // clang-format on
        return {MAP4[idx], false};
    }

    // direction_count == 8
    if (unique_diagonals)
        return {idx, false};

    // Mirrored: 6 unique columns. NE mirrors NW, SE mirrors SW.
    // clang-format off
    static constexpr DirMapping MAP6[8] = {
        {0, false}, // South
        {1, false}, // SouthWest
        {2, false}, // West
        {3, false}, // NorthWest
        {4, false}, // North
        {3, true},  // NorthEast -> mirror NorthWest
        {5, false}, // East
        {1, true},  // SouthEast -> mirror SouthWest
    };
    // clang-format on
    return MAP6[idx];
}

CardinalDir snapMovement(float dx, float dy, int direction_count)
{
    if (direction_count >= 8)
        return snapToOctant(dx, dy);
    return snapToCardinal4(dx, dy);
}

CardinalDir snapFacing(float dx, float dy, CardinalDir current, int direction_count)
{
    if (direction_count >= 8)
        return snapWithHysteresis8(dx, dy, current);
    return snapWithHysteresis4(dx, dy, current);
}

} // namespace engine::direction
