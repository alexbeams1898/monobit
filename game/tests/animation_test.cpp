#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/AnimationSystem.h"
#include "test_helpers.h"
#include "utils/DirectionUtils.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using engine::direction::dirToColumnIndex;
using engine::direction::snapToOctant;
using engine::direction::snapWithHysteresis8;

// ---------------------------------------------------------------------------
// 8-directional sprite support tests.
// No window, no GPU, no SDL required.
// ---------------------------------------------------------------------------

// --- snapToOctant ---

TEST_CASE("snapToOctant pure cardinal directions", "[animation][octant]")
{
    REQUIRE(snapToOctant(0.0f, 1.0f) == CardinalDir::South);
    REQUIRE(snapToOctant(0.0f, -1.0f) == CardinalDir::North);
    REQUIRE(snapToOctant(1.0f, 0.0f) == CardinalDir::East);
    REQUIRE(snapToOctant(-1.0f, 0.0f) == CardinalDir::West);
}

TEST_CASE("snapToOctant pure diagonal directions", "[animation][octant]")
{
    REQUIRE(snapToOctant(1.0f, 1.0f) == CardinalDir::SouthEast);
    REQUIRE(snapToOctant(-1.0f, 1.0f) == CardinalDir::SouthWest);
    REQUIRE(snapToOctant(1.0f, -1.0f) == CardinalDir::NorthEast);
    REQUIRE(snapToOctant(-1.0f, -1.0f) == CardinalDir::NorthWest);
}

TEST_CASE("snapToOctant near-cardinal snaps to cardinal", "[animation][octant]")
{
    // 10 degrees from south (well within cardinal sector)
    REQUIRE(snapToOctant(0.17f, 0.98f) == CardinalDir::South);
    // 10 degrees from east
    REQUIRE(snapToOctant(0.98f, 0.17f) == CardinalDir::East);
}

TEST_CASE("snapToOctant near-diagonal snaps to diagonal", "[animation][octant]")
{
    // 40 degrees from east (5 degrees into diagonal sector)
    const float dx = std::cos(40.0f * 3.14159f / 180.0f);
    const float dy = std::sin(40.0f * 3.14159f / 180.0f);
    REQUIRE(snapToOctant(dx, dy) == CardinalDir::SouthEast);
}

TEST_CASE("snapToOctant zero vector defaults to South", "[animation][octant]")
{
    REQUIRE(snapToOctant(0.0f, 0.0f) == CardinalDir::South);
}

// --- snapWithHysteresis8 ---

TEST_CASE("snapWithHysteresis8 stays in current sector within cone", "[animation][hysteresis]")
{
    // Pointing mostly south, current is south -> should stay south
    REQUIRE(snapWithHysteresis8(0.1f, 0.99f, CardinalDir::South) == CardinalDir::South);
}

TEST_CASE("snapWithHysteresis8 switches when clearly outside sector", "[animation][hysteresis]")
{
    // Pointing east but current is south -> should switch to east
    REQUIRE(snapWithHysteresis8(1.0f, 0.0f, CardinalDir::South) == CardinalDir::East);
}

TEST_CASE("snapWithHysteresis8 resists jitter near boundary", "[animation][hysteresis]")
{
    // 80 degrees from east = 10 degrees from south. Dot with south (0,1) = ~0.98 > 0.887
    // threshold, so hysteresis keeps current direction.
    const float dx = std::cos(80.0f * 3.14159f / 180.0f); // ~0.17
    const float dy = std::sin(80.0f * 3.14159f / 180.0f); // ~0.98
    REQUIRE(snapWithHysteresis8(dx, dy, CardinalDir::South) == CardinalDir::South);
}

TEST_CASE("snapWithHysteresis8 zero vector keeps current", "[animation][hysteresis]")
{
    REQUIRE(snapWithHysteresis8(0.0f, 0.0f, CardinalDir::NorthWest) == CardinalDir::NorthWest);
}

// --- dirToColumnIndex ---

TEST_CASE("dirToColumnIndex direction_count=1 always returns 0", "[animation][column]")
{
    for (int i = 0; i < 8; ++i)
    {
        auto dir = static_cast<CardinalDir>(i);
        auto m = dirToColumnIndex(dir, 1, false);
        REQUIRE(m.column == 0);
        REQUIRE_FALSE(m.flip);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("dirToColumnIndex direction_count=4 maps to 4 columns", "[animation][column]")
{
    // Cardinals map to their own columns
    REQUIRE(dirToColumnIndex(CardinalDir::South, 4, false).column == 0);
    REQUIRE(dirToColumnIndex(CardinalDir::West, 4, false).column == 1);
    REQUIRE(dirToColumnIndex(CardinalDir::East, 4, false).column == 2);
    REQUIRE(dirToColumnIndex(CardinalDir::North, 4, false).column == 3);

    // Diagonals collapse to nearest cardinal
    REQUIRE(dirToColumnIndex(CardinalDir::SouthWest, 4, false).column == 0);
    REQUIRE(dirToColumnIndex(CardinalDir::NorthWest, 4, false).column == 1);
    REQUIRE(dirToColumnIndex(CardinalDir::NorthEast, 4, false).column == 3);
    REQUIRE(dirToColumnIndex(CardinalDir::SouthEast, 4, false).column == 2);

    // No flipping for 4-dir
    for (int i = 0; i < 8; ++i)
    {
        auto dir = static_cast<CardinalDir>(i);
        REQUIRE_FALSE(dirToColumnIndex(dir, 4, false).flip);
    }
}

TEST_CASE("dirToColumnIndex direction_count=8 unique_diagonals=true", "[animation][column]")
{
    for (int i = 0; i < 8; ++i)
    {
        auto dir = static_cast<CardinalDir>(i);
        auto m = dirToColumnIndex(dir, 8, true);
        REQUIRE(m.column == i);
        REQUIRE_FALSE(m.flip);
    }
}

TEST_CASE("dirToColumnIndex direction_count=8 mirrored (unique_diagonals=false)",
          "[animation][column]")
{
    // Non-mirrored directions
    REQUIRE(dirToColumnIndex(CardinalDir::South, 8, false).column == 0);
    REQUIRE(dirToColumnIndex(CardinalDir::SouthWest, 8, false).column == 1);
    REQUIRE(dirToColumnIndex(CardinalDir::West, 8, false).column == 2);
    REQUIRE(dirToColumnIndex(CardinalDir::NorthWest, 8, false).column == 3);
    REQUIRE(dirToColumnIndex(CardinalDir::North, 8, false).column == 4);
    REQUIRE(dirToColumnIndex(CardinalDir::East, 8, false).column == 5);

    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::South, 8, false).flip);
    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::SouthWest, 8, false).flip);
    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::West, 8, false).flip);
    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::NorthWest, 8, false).flip);
    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::North, 8, false).flip);
    REQUIRE_FALSE(dirToColumnIndex(CardinalDir::East, 8, false).flip);

    // Mirrored directions: NE mirrors NW, SE mirrors SW
    auto ne = dirToColumnIndex(CardinalDir::NorthEast, 8, false);
    REQUIRE(ne.column == 3); // same as NorthWest
    REQUIRE(ne.flip);

    auto se = dirToColumnIndex(CardinalDir::SouthEast, 8, false);
    REQUIRE(se.column == 1); // same as SouthWest
    REQUIRE(se.flip);
}

// --- Backward compatibility: 8-dir snapping with 4-dir sheet ---

TEST_CASE("8-dir entity snapping produces valid 4-column indices", "[animation][compat]")
{
    // Entity has direction_count=4 but game uses 8-dir snapping.
    // All 8 octant directions should map to valid 4-dir columns (0-3).
    for (int i = 0; i < 8; ++i)
    {
        auto dir = static_cast<CardinalDir>(i);
        auto m = dirToColumnIndex(dir, 4, false);
        REQUIRE(m.column >= 0);
        REQUIRE(m.column <= 3);
        REQUIRE_FALSE(m.flip);
    }
}

// --- AnimationSystem integration: sprite flip_x ---

TEST_CASE("AnimationSystem sets sprite.flip_x for mirrored diagonals", "[animation][flip]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto e = em.create();
    em.registry().emplace<Transform>(e);
    em.registry().emplace<Velocity>(e, Velocity{-0.7f, -0.7f}); // NorthWest
    em.registry().emplace<MovementIntent>(e, MovementIntent{-0.7f, -0.7f});

    Animation anim;
    anim.direction_count = 8;
    anim.unique_diagonals = false;
    anim.states[static_cast<int>(AnimState::Walk)].frames = 2;
    anim.states[static_cast<int>(AnimState::Walk)].duration = 0.1f;
    anim.state = AnimState::Walk;
    anim.max_frames_per_state = 2;
    em.registry().emplace<Animation>(e, anim);
    em.registry().emplace<Sprite>(e);

    AnimationSystem::update(em, 0.016f);

    const auto& spr = em.registry().get<Sprite>(e);
    // NorthWest is NOT mirrored, so flip_x should be false
    REQUIRE_FALSE(spr.flip_x);
}

TEST_CASE("AnimationSystem sets flip_x=true for NorthEast mirrored", "[animation][flip]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto e = em.create();
    em.registry().emplace<Transform>(e);
    em.registry().emplace<Velocity>(e, Velocity{0.7f, -0.7f}); // NorthEast
    em.registry().emplace<MovementIntent>(e, MovementIntent{0.7f, -0.7f});

    Animation anim;
    anim.direction_count = 8;
    anim.unique_diagonals = false;
    anim.states[static_cast<int>(AnimState::Walk)].frames = 2;
    anim.states[static_cast<int>(AnimState::Walk)].duration = 0.1f;
    anim.state = AnimState::Walk;
    anim.max_frames_per_state = 2;
    em.registry().emplace<Animation>(e, anim);
    em.registry().emplace<Sprite>(e);

    AnimationSystem::update(em, 0.016f);

    const auto& spr = em.registry().get<Sprite>(e);
    // NorthEast IS mirrored from NorthWest -> flip_x = true
    REQUIRE(spr.flip_x);
}
