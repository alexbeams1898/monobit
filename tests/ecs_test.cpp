#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// ECS smoke tests — no window, no GPU, no SDL required.
// These verify that the EntityManager and component structs behave correctly
// using entt's API directly.
// ---------------------------------------------------------------------------

TEST_CASE("Entity creation returns a valid entity", "[ecs]")
{
    EntityManager em;
    auto e = em.create();
    REQUIRE(em.registry().valid(e));
}

TEST_CASE("Components can be attached and retrieved", "[ecs]")
{
    EntityManager em;
    auto e = em.create();

    em.registry().emplace<Transform>(e, 10.0f, 20.0f);
    em.registry().emplace<Health>(e, 75, 100);

    auto& t = em.registry().get<Transform>(e);
    auto& h = em.registry().get<Health>(e);

    REQUIRE(t.x == 10.0f);
    REQUIRE(t.y == 20.0f);
    REQUIRE(h.current == 75);
    REQUIRE(h.max == 100);
}

TEST_CASE("Component values persist after modification", "[ecs]")
{
    EntityManager em;
    auto e = em.create();
    em.registry().emplace<Transform>(e);

    em.registry().get<Transform>(e).x = 99.0f;

    REQUIRE(em.registry().get<Transform>(e).x == 99.0f);
}

TEST_CASE("View only returns entities with all queried components", "[ecs]")
{
    EntityManager em;

    auto e1 = em.create();
    auto e2 = em.create();
    auto e3 = em.create();

    em.registry().emplace<Transform>(e1);
    em.registry().emplace<Velocity>(e1);

    em.registry().emplace<Transform>(e2); // no Velocity

    em.registry().emplace<Transform>(e3);
    em.registry().emplace<Velocity>(e3);

    int count = 0;
    for ([[maybe_unused]] auto entity : em.registry().view<Transform, Velocity>())
        ++count;

    REQUIRE(count == 2); // only e1 and e3 match
}

TEST_CASE("Destroyed entity is no longer valid", "[ecs]")
{
    EntityManager em;
    auto e = em.create();
    REQUIRE(em.registry().valid(e));

    em.destroy(e);
    REQUIRE_FALSE(em.registry().valid(e));
}
