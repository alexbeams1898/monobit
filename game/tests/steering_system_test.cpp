#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/SteeringSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// SteeringSystem tests -- wall repulsion + crowd repulsion for NavAgent entities.
//
// SteeringSystem needs no window, GPU, or SDL. Each test verifies one
// behavioral guarantee of the repulsion forces.
//
// Key constants (from SteeringSystem.cpp, repeated here for documentation):
//   REPULSION_RADIUS     = 20.0f px from wall surface -- wall force starts here
//   REPULSION_STRENGTH   = 0.5f  -- wall force multiplier
//   CROWD_SAMPLE_RADIUS  = 2 cells -- neighbour window for crowd repulsion
// ---------------------------------------------------------------------------

static entt::entity makeEnemy(EntityManager& em, float x, float y, float /*speed*/, float vx = 0.0f,
                              float vy = 0.0f, float separation_strength = 1.0f)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Velocity>(e, Velocity{vx, vy});
    em.registry().emplace<NavAgent>(e, NavAgent{separation_strength});
    return e;
}

static entt::entity makeWall(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{32.0f, 32.0f, true});
    // No Velocity -- treated as a static wall.
    return e;
}

TEST_CASE("SteeringSystem does nothing when no walls are nearby", "[steering]")
{
    EntityManager em;
    // Wall center (200,0): surface starts at x=184. Entity at (0,0): dist=184 >> 20.
    makeWall(em, 200.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 80.0f, 0.0f, -80.0f);

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(-80.0f));
}

TEST_CASE("SteeringSystem deflects velocity away from a nearby wall", "[steering]")
{
    EntityManager em;
    // Wall center (32,50) 32x32 -> west surface at x=16.
    // Entity center (0,50): closest wall point = (16,50), dist=16 < REPULSION_RADIUS=20.
    // Entity moving north (0,-80).
    // Expected: vel.dx < 0 (westward nudge away from wall), vel.dy < 0 (still north).
    makeWall(em, 32.0f, 50.0f);
    auto enemy = makeEnemy(em, 0.0f, 50.0f, 80.0f, 0.0f, -80.0f);

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx < 0.0f); // deflected west -- away from east wall
    REQUIRE(vel.dy < 0.0f); // still heading north
}

TEST_CASE("SteeringSystem preserves entity speed after deflection", "[steering]")
{
    EntityManager em;
    makeWall(em, 32.0f, 50.0f);
    auto enemy = makeEnemy(em, 0.0f, 50.0f, 80.0f, 0.0f, -80.0f);

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    const float mag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    REQUIRE(mag == Catch::Approx(80.0f).margin(0.5f));
}

TEST_CASE("SteeringSystem does not affect entities without NavAgent", "[steering]")
{
    EntityManager em;
    // Same wall/entity setup as the deflection test, but no NavAgent.
    makeWall(em, 32.0f, 50.0f);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{0.0f, 50.0f});
    em.registry().emplace<Velocity>(e, Velocity{0.0f, -80.0f});

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(e);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(-80.0f));
}

TEST_CASE("SteeringSystem does not repel from a wall directly ahead", "[steering]")
{
    // Regression: entity heading south, wall directly to the south.
    // Without the dot-product filter the repulsion opposes forward motion and
    // the entity bounces at the door threshold. With the filter, the head-on
    // wall is skipped (dot ~ -1.0 < SKIP_DOT_THRESHOLD) and velocity is
    // unchanged -- MovementSystem handles the actual blocking.
    EntityManager em;
    // Wall center (0,32): north surface at y=16. Entity at (0,0) heading
    // south (vel.dy=+80). Closest wall point = (0,16), dist=16 < 20 -> in range.
    // dot(south=(0,1), repulsion=(0,-1)) = -1 < -0.5 -> must be skipped.
    makeWall(em, 0.0f, 32.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 80.0f, 0.0f, 80.0f);

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(80.0f)); // forward velocity untouched
}

TEST_CASE("SteeringSystem does not affect stopped entities", "[steering]")
{
    EntityManager em;
    // Entity velocity = (0,0). Repulsion is skipped to avoid divide-by-zero
    // in the renormalization step and to avoid artificially launching a stopped
    // entity during the velocity-blending warmup frames.
    makeWall(em, 32.0f, 50.0f);
    auto enemy = makeEnemy(em, 0.0f, 50.0f, 80.0f, 0.0f, 0.0f);

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// Crowd repulsion tests
// ---------------------------------------------------------------------------

TEST_CASE("SteeringSystem crowd repulsion deflects away from dense neighbour", "[steering][crowd]")
{
    EntityManager em;
    // Enemy at (100,100) heading right. Cell: col=6, row=6.
    // Place 3 enemies in the cell BELOW (row=7, col=6) -- crowd is lateral.
    // Expected: vel.dy < 0 (pushed north, away from south crowd).
    auto enemy = makeEnemy(em, 100.0f, 100.0f, 80.0f, 80.0f, 0.0f, 1.0f);

    em.flow_field.density[7][6] = 3; // 3 enemies one cell to the south

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dy < 0.0f); // deflected north -- away from south crowd
    REQUIRE(vel.dx > 0.0f); // still has rightward component
}

TEST_CASE("SteeringSystem crowd repulsion preserves speed for lateral crowd", "[steering][crowd]")
{
    // Lateral crowd (south) adds a perpendicular component; combined vector
    // exceeds origSpeed and is capped back to it.
    EntityManager em;
    auto enemy = makeEnemy(em, 100.0f, 100.0f, 80.0f, 80.0f, 0.0f, 1.0f);

    em.flow_field.density[7][6] = 3; // lateral -- perpendicular to vel

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    const float mag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    REQUIRE(mag == Catch::Approx(80.0f).margin(0.5f));
}

TEST_CASE("SteeringSystem crowd repulsion disabled when separation_strength is zero",
          "[steering][crowd]")
{
    EntityManager em;
    auto enemy = makeEnemy(em, 100.0f, 100.0f, 80.0f, 80.0f, 0.0f, 0.0f); // disabled

    em.flow_field.density[6][7] = 10; // high density -- but should be ignored

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(80.0f)); // unchanged
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("SteeringSystem same-cell repulsion deflects toward own cell edge", "[steering][crowd]")
{
    EntityManager em;
    // Enemy at (108,100) heading east -- cell (6,6), cell centre (104,104).
    // Entity is NE of the cell centre: offset = (+4,-4), offN ~ (+0.71,-0.71).
    // density[6][6] = 2 -> same-cell pass fires, weight = (2-1)*2 = 2.
    // dot(east=(1,0), offN=(+0.71,-0.71)) = +0.71 >= SKIP_DOT_THRESHOLD -> not skipped.
    // Force pushes NE -- vel.dy becomes negative (northward component added).
    auto enemy = makeEnemy(em, 108.0f, 100.0f, 80.0f, 80.0f, 0.0f, 1.0f);
    em.flow_field.density[6][6] = 2;

    SteeringSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dy < 0.0f); // pushed north -- entity is NE of cell centre
    REQUIRE(vel.dx > 0.0f); // still has eastward component
}
