#include "utils/DirectionUtils.h"

#include <catch2/catch_test_macros.hpp>

// A 2-direction sheet holds ONE drawing per character and mirrors it, rather than a row per
// facing. These cover the two properties that make that work: the mirror is the only thing
// carrying direction, and vertical movement must not disturb it.

using engine::direction::dirToColumnIndex;
using engine::direction::snapFacing;
using engine::direction::snapMovement;

TEST_CASE("mirrored sheets have one column and flip for west", "[direction]")
{
    // Art is authored facing east, so east is the unmirrored pose.
    const auto east = dirToColumnIndex(CardinalDir::East, 2);
    CHECK(east.column == 0);
    CHECK_FALSE(east.flip);

    const auto west = dirToColumnIndex(CardinalDir::West, 2);
    CHECK(west.column == 0);
    CHECK(west.flip);
}

TEST_CASE("omnidirectional sheets never flip", "[direction]")
{
    // direction_count 1 means "one pose, no facing at all" -- distinct from the mirrored case,
    // and the distinction matters: forcing flip off here is correct, and doing the same for a
    // mirrored sheet would leave it permanently facing one way.
    CHECK_FALSE(dirToColumnIndex(CardinalDir::West, 1).flip);
    CHECK(dirToColumnIndex(CardinalDir::West, 1).column == 0);
}

TEST_CASE("vertical movement keeps the side already faced", "[direction]")
{
    // There is no north or south drawing, so walking straight up carries no facing information.
    // Snapping to a default here is what makes a character appear to turn when he does not.
    CHECK(snapFacing(0.0f, -1.0f, CardinalDir::West, 2) == CardinalDir::West);
    CHECK(snapFacing(0.0f, 1.0f, CardinalDir::East, 2) == CardinalDir::East);

    // Standing still likewise holds.
    CHECK(snapFacing(0.0f, 0.0f, CardinalDir::West, 2) == CardinalDir::West);
}

TEST_CASE("horizontal movement sets the side, diagonals included", "[direction]")
{
    CHECK(snapFacing(1.0f, 0.0f, CardinalDir::West, 2) == CardinalDir::East);
    CHECK(snapFacing(-1.0f, 0.0f, CardinalDir::East, 2) == CardinalDir::West);

    // A diagonal has a horizontal component, so it turns him -- no hysteresis needed when there
    // are only two sides to choose between.
    CHECK(snapFacing(1.0f, -1.0f, CardinalDir::West, 2) == CardinalDir::East);
    CHECK(snapFacing(-1.0f, 1.0f, CardinalDir::East, 2) == CardinalDir::West);
}

TEST_CASE("movement snapping mirrors facing snapping", "[direction]")
{
    CHECK(snapMovement(1.0f, 0.0f, 2) == CardinalDir::East);
    CHECK(snapMovement(-1.0f, 0.0f, 2) == CardinalDir::West);
}

TEST_CASE("four-direction sheets are unaffected", "[direction]")
{
    // The mirrored path must not disturb the row-per-direction convention.
    CHECK(dirToColumnIndex(CardinalDir::South, 4).column == 0);
    CHECK(dirToColumnIndex(CardinalDir::West, 4).column == 1);
    CHECK(dirToColumnIndex(CardinalDir::East, 4).column == 2);
    CHECK(dirToColumnIndex(CardinalDir::North, 4).column == 3);
    CHECK_FALSE(dirToColumnIndex(CardinalDir::West, 4).flip);
}
