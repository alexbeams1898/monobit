#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/MovementSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// MovementSystem tests — no window, no GPU, no SDL required.
// ---------------------------------------------------------------------------

TEST_CASE("MovementSystem translates rightward input into position change", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 100.0f});
    em.registry().emplace<Velocity>(e);
    PlayerActions pa;
    pa.move_x = 1.0f;
    em.registry().emplace<PlayerActions>(e, pa);

    MovementSystem::update(em, 1.0); // 1 second at 150 px/s (base, no Stats)

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(250.0f)); // 100 + 150*1
    REQUIRE(t.y == Catch::Approx(100.0f)); // unchanged
}

TEST_CASE("MovementSystem translates leftward input into negative x movement", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{300.0f, 100.0f});
    em.registry().emplace<Velocity>(e);
    PlayerActions pa;
    pa.move_x = -1.0f;
    em.registry().emplace<PlayerActions>(e, pa);

    MovementSystem::update(em, 0.5); // half a second at 150 px/s (base, no Stats)

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(225.0f).margin(0.1f)); // 300 - 150*0.5, ±0.1px for blend
    REQUIRE(t.y == Catch::Approx(100.0f));
}

TEST_CASE("MovementSystem does not move entity with zero input", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{50.0f, 75.0f});
    em.registry().emplace<Velocity>(e);
    em.registry().emplace<PlayerActions>(e);

    MovementSystem::update(em, 1.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(50.0f));
    REQUIRE(t.y == Catch::Approx(75.0f));
}

TEST_CASE("MovementSystem does not move entity when dt is zero", "[movement]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 200.0f});
    em.registry().emplace<Velocity>(e);
    PlayerActions pa;
    pa.move_x = 1.0f;
    pa.move_y = 1.0f;
    em.registry().emplace<PlayerActions>(e, pa);

    MovementSystem::update(em, 0.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f));
    REQUIRE(t.y == Catch::Approx(200.0f));
}

TEST_CASE("MovementSystem integrates pre-existing velocity without PlayerActions", "[movement]")
{
    // Pass 2 (Velocity -> Transform) must run for entities that have no PlayerActions --
    // this is how AI-controlled entities move.
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{0.0f, 0.0f});
    em.registry().emplace<Velocity>(e, Velocity{50.0f, -30.0f});

    MovementSystem::update(em, 2.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f)); // 50*2
    REQUIRE(t.y == Catch::Approx(-60.0f)); // -30*2
}

TEST_CASE("MovementSystem entity with PlayerActions but no Velocity does not move", "[movement]")
{
    // PlayerActions alone is not enough -- the entity also needs a Velocity component.
    EntityManager em;
    emplaceGameConfigs(em);
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{100.0f, 100.0f});
    PlayerActions pa;
    pa.move_x = 1.0f;
    pa.move_y = 1.0f;
    em.registry().emplace<PlayerActions>(e, pa);

    MovementSystem::update(em, 1.0);

    auto& t = em.registry().get<Transform>(e);
    REQUIRE(t.x == Catch::Approx(100.0f));
    REQUIRE(t.y == Catch::Approx(100.0f));
}
