#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/MovementSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// MovementSystem tests — no window, no GPU, no SDL required.
//
// InputSystem is NOT unit-tested here because it reads SDL_GetKeyboardState,
// which requires SDL to be initialised and hardware key state to be injected.
// It is covered by running the game (integration test). MovementSystem owns
// all the arithmetic, so that is where the unit tests live.
// ---------------------------------------------------------------------------

TEST_CASE("MovementSystem translates rightward input into position change", "[movement]")
{
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 100.0f});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<Input>(e, Input{1.0f, 0.0f});

    MovementSystem::update(em, 1.0); // 1 second at 150 px/s (base, no Stats)

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(250.0f)); // 100 + 150*1
    REQUIRE(t.y == Catch::Approx(100.0f)); // unchanged
}

TEST_CASE("MovementSystem translates leftward input into negative x movement", "[movement]")
{
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{300.0f, 100.0f});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<Input>(e, Input{-1.0f, 0.0f});

    MovementSystem::update(em, 0.5); // half a second at 150 px/s (base, no Stats)

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(225.0f).margin(0.1f)); // 300 - 150*0.5, ±0.1px for blend
    REQUIRE(t.y == Catch::Approx(100.0f));
}

TEST_CASE("MovementSystem does not move entity with zero input", "[movement]")
{
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{50.0f, 75.0f});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<Input>(e); // move_x=0, move_y=0 by default

    MovementSystem::update(em, 1.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(50.0f));
    REQUIRE(t.y == Catch::Approx(75.0f));
}

TEST_CASE("MovementSystem does not move entity when dt is zero", "[movement]")
{
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 200.0f});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<Input>(e, Input{1.0f, 1.0f});

    MovementSystem::update(em, 0.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f));
    REQUIRE(t.y == Catch::Approx(200.0f));
}

TEST_CASE("MovementSystem integrates pre-existing velocity without an Input component",
          "[movement]")
{
    // Pass 2 (Velocity → Transform) must run for entities that have no Input —
    // this is how AI-controlled entities will move once AI systems are added.
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{0.0f, 0.0f});
    em.registry().emplace<Velocity>(e, Velocity{50.0f, -30.0f}); // no Input

    MovementSystem::update(em, 2.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f)); // 50*2
    REQUIRE(t.y == Catch::Approx(-60.0f)); // -30*2
}

TEST_CASE("MovementSystem entity with Input but no Velocity does not move", "[movement]")
{
    // Input alone is not enough — the entity also needs a Velocity component.
    // (Pass 1 requires both Input and Velocity; Pass 2 requires Velocity and Transform.)
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 100.0f});
    em.registry().emplace<Input>(e, Input{1.0f, 1.0f}); // no Velocity

    MovementSystem::update(em, 1.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f));
    REQUIRE(t.y == Catch::Approx(100.0f));
}
