#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/AnimationSystem.h"
#include "test_helpers.h"
#include "utils/DirectionUtils.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using engine::direction::dirToColumnIndex;
using engine::direction::snapFacing;
using engine::direction::snapMovement;

// ---------------------------------------------------------------------------
// 4-directional sprite support tests.
// No window, no GPU, no SDL required.
// ---------------------------------------------------------------------------

// --- snapMovement (4-dir snap from velocity vector) ---

TEST_CASE("snapMovement pure cardinals", "[animation][snap]")
{
    REQUIRE(snapMovement(0.0f, 1.0f, 4) == CardinalDir::South);
    REQUIRE(snapMovement(0.0f, -1.0f, 4) == CardinalDir::North);
    REQUIRE(snapMovement(1.0f, 0.0f, 4) == CardinalDir::East);
    REQUIRE(snapMovement(-1.0f, 0.0f, 4) == CardinalDir::West);
}

TEST_CASE("snapMovement diagonals pick dominant axis, ties prefer vertical", "[animation][snap]")
{
    // Exact diagonal: dominant axis tie -> vertical wins.
    REQUIRE(snapMovement(0.707f, 0.707f, 4) == CardinalDir::South);
    REQUIRE(snapMovement(-0.707f, -0.707f, 4) == CardinalDir::North);

    // Slightly more horizontal -> east/west wins.
    REQUIRE(snapMovement(0.8f, 0.6f, 4) == CardinalDir::East);
    REQUIRE(snapMovement(-0.8f, -0.6f, 4) == CardinalDir::West);

    // Slightly more vertical -> north/south wins.
    REQUIRE(snapMovement(0.6f, 0.8f, 4) == CardinalDir::South);
    REQUIRE(snapMovement(0.6f, -0.8f, 4) == CardinalDir::North);
}

TEST_CASE("snapMovement direction_count=1 returns South", "[animation][snap]")
{
    // Static props stay at their default direction regardless of velocity.
    REQUIRE(snapMovement(1.0f, 0.0f, 1) == CardinalDir::South);
    REQUIRE(snapMovement(0.0f, -1.0f, 1) == CardinalDir::South);
}

// --- snapFacing (hysteresis) ---

TEST_CASE("snapFacing stays on current axis when close to diagonal", "[animation][hysteresis]")
{
    // Vector is exactly diagonal. Starting from South (vertical), hysteresis
    // should keep it on the vertical axis.
    REQUIRE(snapFacing(0.707f, 0.707f, CardinalDir::South, 4) == CardinalDir::South);
    REQUIRE(snapFacing(-0.707f, -0.707f, CardinalDir::North, 4) == CardinalDir::North);

    // Starting from East (horizontal), same diagonal should stay horizontal.
    REQUIRE(snapFacing(0.707f, 0.707f, CardinalDir::East, 4) == CardinalDir::East);
    REQUIRE(snapFacing(-0.707f, -0.707f, CardinalDir::West, 4) == CardinalDir::West);
}

TEST_CASE("snapFacing switches when other axis clearly dominates", "[animation][hysteresis]")
{
    // Starting from South, strong eastward vector -> switch to East.
    REQUIRE(snapFacing(0.99f, 0.1f, CardinalDir::South, 4) == CardinalDir::East);
    // Starting from East, strong southward vector -> switch to South.
    REQUIRE(snapFacing(0.1f, 0.99f, CardinalDir::East, 4) == CardinalDir::South);
}

TEST_CASE("snapFacing resists jitter near the diagonal threshold", "[animation][hysteresis]")
{
    // Slight 5% horizontal dominance. Starting from South (vertical),
    // hysteresis requires the other axis to exceed +15% to flip.
    REQUIRE(snapFacing(0.51f, 0.49f, CardinalDir::South, 4) == CardinalDir::South);
    // Same vector starting from East stays East.
    REQUIRE(snapFacing(0.51f, 0.49f, CardinalDir::East, 4) == CardinalDir::East);
}

TEST_CASE("snapFacing direction_count=1 keeps current", "[animation][hysteresis]")
{
    REQUIRE(snapFacing(1.0f, 0.0f, CardinalDir::South, 1) == CardinalDir::South);
    REQUIRE(snapFacing(0.0f, -1.0f, CardinalDir::South, 1) == CardinalDir::South);
}

// --- dirToColumnIndex ---

TEST_CASE("dirToColumnIndex direction_count=1 always returns column 0", "[animation][column]")
{
    for (int i = 0; i < 4; ++i)
    {
        const auto dir = static_cast<CardinalDir>(i);
        const auto m = dirToColumnIndex(dir, 1);
        REQUIRE(m.column == 0);
        REQUIRE_FALSE(m.flip);
    }
}

TEST_CASE("dirToColumnIndex direction_count=4 maps each cardinal to its own column",
          "[animation][column]")
{
    REQUIRE(dirToColumnIndex(CardinalDir::South, 4).column == 0);
    REQUIRE(dirToColumnIndex(CardinalDir::West, 4).column == 1);
    REQUIRE(dirToColumnIndex(CardinalDir::East, 4).column == 2);
    REQUIRE(dirToColumnIndex(CardinalDir::North, 4).column == 3);

    // 4-dir mapping never flips horizontally.
    for (int i = 0; i < 4; ++i)
    {
        const auto dir = static_cast<CardinalDir>(i);
        REQUIRE_FALSE(dirToColumnIndex(dir, 4).flip);
    }
}

// --- AnimationSystem integration: sprite src_x tracks direction ---

TEST_CASE("AnimationSystem writes src_x for 4-dir character based on velocity direction",
          "[animation][src_x]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto e = em.create();
    em.registry().emplace<Transform>(e);
    // Moving West (no FacingDirection component, so snapMovement uses velocity).
    em.registry().emplace<Velocity>(e, Velocity{-1.0f, 0.0f});
    em.registry().emplace<MovementIntent>(e, MovementIntent{-1.0f, 0.0f});

    Animation anim;
    anim.direction_count = 4;
    anim.current_frames = 2;
    anim.current_duration = 0.1f;
    anim.current_row = 1;
    anim.max_frames_per_state = 2;
    em.registry().emplace<Animation>(e, anim);
    em.registry().emplace<Sprite>(e);

    AnimationSystem::update(em, 0.016f);

    const auto& spr = em.registry().get<Sprite>(e);
    // West = column 1, frame 0 -> src_x = (1 * 2 + 0) * frame_width.
    // Default frame_width is 32 from Animation's default.
    REQUIRE(spr.src_x == (1 * 2 + 0) * 32);
    // 4-dir never flips.
    REQUIRE_FALSE(spr.flip_x);
}
