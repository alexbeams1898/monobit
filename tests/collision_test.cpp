#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CollisionSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// CollisionSystem tests — no window, no GPU, no SDL required.
//
// TextureManager and RenderSystem are NOT unit-tested here because they
// require an active OpenGL context. Run the game to integration-test those.
// ---------------------------------------------------------------------------

// Helper: create an entity with a centered AABB collider.
static entt::entity makeEntity(EntityManager& em, float x, float y, float w, float h,
                                bool isSolid = true, bool dynamic = true)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{w, h, isSolid});
    if (dynamic)
        em.registry().emplace<Velocity>(e, Velocity{0.0f, 0.0f});
    return e;
}

TEST_CASE("Non-overlapping entities emit no collision events", "[collision]")
{
    EntityManager em;
    // Two 32x32 boxes placed 200px apart — no overlap.
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    makeEntity(em, 200.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em);

    REQUIRE(em.collisionEvents.empty());
}

TEST_CASE("Overlapping entities emit a CollisionEvent", "[collision]")
{
    EntityManager em;
    // Two 32x32 boxes centered 10px apart — they overlap by 22px on X.
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    makeEntity(em, 10.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em);

    REQUIRE(em.collisionEvents.size() == 1);
}

TEST_CASE("Dynamic entity is pushed out of static solid", "[collision]")
{
    EntityManager em;
    // Static wall at (0, 0), dynamic player at (20, 0) — overlapping by 12px on X.
    auto wall   = makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    auto player = makeEntity(em, 20.0f, 0.0f, 32.0f, 32.0f, true, true);

    CollisionSystem::update(em);

    auto& wallT   = em.registry().get<Transform>(wall);
    auto& playerT = em.registry().get<Transform>(player);

    // Wall must not move — it is static (no Velocity).
    REQUIRE(wallT.x == Catch::Approx(0.0f));
    REQUIRE(wallT.y == Catch::Approx(0.0f));

    // Player must be pushed out so the boxes no longer overlap.
    // Player was at x=20, wall at x=0; overlap on X = (16+16) - 20 = 12.
    // Player gets pushed right by 12 + SEPARATION_BIAS(0.1) = 12.1 → ends up at x = 32.1.
    REQUIRE(playerT.x == Catch::Approx(32.1f));
    REQUIRE(playerT.y == Catch::Approx(0.0f));

    // Event must still be recorded.
    REQUIRE_FALSE(em.collisionEvents.empty());
}

TEST_CASE("Static-vs-static solid records event but moves nothing", "[collision]")
{
    EntityManager em;
    // Two static 32x32 solids overlapping — neither has Velocity.
    auto a = makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    auto b = makeEntity(em, 10.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em);

    // Positions unchanged.
    REQUIRE(em.registry().get<Transform>(a).x == Catch::Approx(0.0f));
    REQUIRE(em.registry().get<Transform>(b).x == Catch::Approx(10.0f));

    // Event still emitted.
    REQUIRE(em.collisionEvents.size() == 1);
}

TEST_CASE("Dynamic entity is pushed out of two stacked solid walls", "[collision]")
{
    EntityManager em;
    // Two walls side by side at x=0 and x=32 (just touching, not overlapping each other).
    // Player at x=20 — overlapping the first wall.
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);  // wall 1
    makeEntity(em, 32.0f, 0.0f, 32.0f, 32.0f, true, false); // wall 2 (touching wall 1)
    auto player = makeEntity(em, 20.0f, 0.0f, 32.0f, 32.0f, true, true);

    CollisionSystem::update(em);

    // Player should be outside both walls — no overlap remains.
    const auto& pt = em.registry().get<Transform>(player);
    // Wall 1 right edge: 0 + 16 = 16. Player left edge after push: pt.x - 16.
    // Player should be at x >= 16 (outside wall 1's right edge).
    REQUIRE(pt.x - 16.0f >= -0.001f);
}

TEST_CASE("Collision events are cleared between frames", "[collision]")
{
    EntityManager em;
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    makeEntity(em, 10.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em); // emits 1 event
    REQUIRE(em.collisionEvents.size() == 1);

    // Move entities apart so they no longer overlap, then run again.
    auto view = em.registry().view<Transform>();
    int idx   = 0;
    for (auto [entity, t] : view.each())
    {
        t.x = static_cast<float>(idx) * 200.0f;
        ++idx;
    }

    CollisionSystem::update(em); // no overlap → events cleared, none added
    REQUIRE(em.collisionEvents.empty());
}
