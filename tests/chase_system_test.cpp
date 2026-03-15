#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/ChaseSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// ---------------------------------------------------------------------------
// ChaseSystem tests — no window, no GPU, no SDL required.
//
// ChaseSystem is pure arithmetic: it reads Transform positions and writes
// Velocity values. All cases here are fully headless.
// ---------------------------------------------------------------------------

// Helpers — create a player entity (has Input tag) and an enemy entity.
static entt::entity makePlayer(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Input>(e);
    return e;
}

static entt::entity makeEnemy(EntityManager& em, float x, float y, float speed,
                              AIController::State state = AIController::State::Chase)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Velocity>(e);
    AIController ai;
    ai.state = state;
    ai.speed = speed;
    em.registry().emplace<AIController>(e, ai);
    return e;
}

TEST_CASE("ChaseSystem moves entity directly toward player on x-axis", "[chase]")
{
    EntityManager em;
    makePlayer(em, 100.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    ChaseSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(100.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem normalizes diagonal direction correctly", "[chase]")
{
    EntityManager em;
    makePlayer(em, 100.0f, 100.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    ChaseSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    const float expected = 100.0f / std::sqrt(2.0f);
    REQUIRE(vel.dx == Catch::Approx(expected));
    REQUIRE(vel.dy == Catch::Approx(expected));
}

TEST_CASE("ChaseSystem speed is preserved regardless of distance", "[chase]")
{
    // Velocity magnitude should equal ai.speed, not raw distance.
    EntityManager em;
    makePlayer(em, 500.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 80.0f);

    ChaseSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    const float mag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    REQUIRE(mag == Catch::Approx(80.0f));
}

TEST_CASE("ChaseSystem does not move idle entity", "[chase]")
{
    EntityManager em;
    makePlayer(em, 100.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f, AIController::State::Idle);

    // Pre-set a non-zero velocity to confirm it is left untouched.
    em.registry().get<Velocity>(enemy) = {5.0f, 5.0f};

    ChaseSystem::update(em);

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(5.0f));
    REQUIRE(vel.dy == Catch::Approx(5.0f));
}

TEST_CASE("ChaseSystem does nothing when no player exists", "[chase]")
{
    EntityManager em;
    // No player entity (no Input component) — system should early-out safely.
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f);

    ChaseSystem::update(em); // must not crash

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem does not divide by zero when enemy is on top of player", "[chase]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 100.0f); // same position

    ChaseSystem::update(em); // must not produce NaN or crash

    const auto& vel = em.registry().get<Velocity>(enemy);
    REQUIRE(vel.dx == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("ChaseSystem handles multiple enemies independently", "[chase]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);

    // Enemy A is directly to the right.
    auto enemyA = makeEnemy(em, 100.0f, 0.0f, 50.0f);
    // Enemy B is directly below.
    auto enemyB = makeEnemy(em, 0.0f, 200.0f, 120.0f);

    ChaseSystem::update(em);

    const auto& velA = em.registry().get<Velocity>(enemyA);
    REQUIRE(velA.dx == Catch::Approx(-50.0f)); // moving left toward player
    REQUIRE(velA.dy == Catch::Approx(0.0f));

    const auto& velB = em.registry().get<Velocity>(enemyB);
    REQUIRE(velB.dx == Catch::Approx(0.0f));
    REQUIRE(velB.dy == Catch::Approx(-120.0f)); // moving up toward player
}
