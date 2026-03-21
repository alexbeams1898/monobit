#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/MovementSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// MovementSystem tests — no window, no GPU, no SDL required.
//
// MovementSystem owns static-wall collision via velocity projection. Before
// integrating each frame, it tests X and Y axes independently against every
// static solid using a slightly inset bounding box (MOVEMENT_INSET = 2 px).
// This inset prevents "corner sticking" — getting caught on adjacent tile
// corners when sliding along a wall — while still blocking head-on collisions.
//
// InputMappingSystem depends on SDL keyboard state and is integration-tested by
// running the game. Here we set Velocity directly to test the projection path.
// ---------------------------------------------------------------------------

// dt used for all tests — 1/60 s, matching the fixed timestep.
static constexpr double DT = 1.0 / 60.0;

// Helper: create a dynamic entity (has Velocity + Collider) at (x,y).
static entt::entity makeDynamic(EntityManager& em, float x, float y, float w = 32.0f,
                                float h = 32.0f)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{w, h, true});
    em.registry().emplace<Velocity>(e, Velocity{0.0f, 0.0f});
    return e;
}

// Helper: create a static wall (no Velocity) at (x,y).
static entt::entity makeWall(EntityManager& em, float x, float y, float w = 32.0f, float h = 32.0f)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{w, h, true});
    return e;
}

TEST_CASE("Entity with no walls nearby moves freely", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = makeDynamic(em, 0.0f, 0.0f);
    em.registry().get<Velocity>(e) = {100.0f, 50.0f};

    MovementSystem::update(em, DT);

    const auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f * static_cast<float>(DT)));
    REQUIRE(t.y == Catch::Approx(50.0f * static_cast<float>(DT)));
}

TEST_CASE("Entity moving right blocked by wall: X velocity zeroed", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Entity at (0,0) moving right at 200 px/s → moves 3.33 px in 1/60 s.
    // Effective (inset) half-width = (32-2)/2 = 15. New right edge = 3.33+15 = 18.33.
    // Wall at (33,0): left edge = 33-16 = 17. Overlap = 18.33-17 = 1.33 px → blocked.
    auto entity = makeDynamic(em, 0.0f, 0.0f);
    makeWall(em, 33.0f, 0.0f);
    em.registry().get<Velocity>(entity) = {200.0f, 0.0f};

    MovementSystem::update(em, DT);

    const auto& t = em.registry().get<Transform>(entity);
    const auto& vel = em.registry().get<Velocity>(entity);

    REQUIRE(t.x == Catch::Approx(0.0f));    // did not move
    REQUIRE(vel.dx == Catch::Approx(0.0f)); // velocity zeroed
}

TEST_CASE("Entity moving down blocked by wall: Y velocity zeroed", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto entity = makeDynamic(em, 0.0f, 0.0f);
    makeWall(em, 0.0f, 33.0f); // wall just below — same inset math as X test
    em.registry().get<Velocity>(entity) = {0.0f, 200.0f};

    MovementSystem::update(em, DT);

    const auto& t = em.registry().get<Transform>(entity);
    const auto& vel = em.registry().get<Velocity>(entity);

    REQUIRE(t.y == Catch::Approx(0.0f));
    REQUIRE(vel.dy == Catch::Approx(0.0f));
}

TEST_CASE("Entity slides along wall: blocked on X, free on Y", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Wall to the right, no wall below — entity should move only on Y.
    auto entity = makeDynamic(em, 0.0f, 0.0f);
    makeWall(em, 33.0f, 0.0f); // blocks X
    em.registry().get<Velocity>(entity) = {200.0f, 100.0f};

    MovementSystem::update(em, DT);

    const auto& t = em.registry().get<Transform>(entity);
    const auto& vel = em.registry().get<Velocity>(entity);

    // X blocked.
    REQUIRE(t.x == Catch::Approx(0.0f));
    REQUIRE(vel.dx == Catch::Approx(0.0f));

    // Y free — moved by 100 * DT.
    REQUIRE(t.y == Catch::Approx(100.0f * static_cast<float>(DT)));
    REQUIRE(vel.dy == Catch::Approx(100.0f));
}

TEST_CASE("Entity with no collider integrates unconditionally", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    // Entity with Velocity but no Collider — no projection, free movement.
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{0.0f, 0.0f});
    em.registry().emplace<Velocity>(e, Velocity{60.0f, 60.0f});

    makeWall(em, 1.0f, 0.0f); // wall extremely close — would block if collider existed

    MovementSystem::update(em, DT);

    const auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(60.0f * static_cast<float>(DT)));
}
