#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CollisionSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// CollisionSystem tests — no window, no GPU, no SDL required.
//
// Since issue/8, CollisionSystem's responsibility is narrowed:
//   - Emit CollisionEvent for every overlapping Collider pair.
//   - Apply position correction ONLY for dynamic-vs-dynamic pairs (both have
//     Velocity). Static-vs-dynamic correction is handled upstream by
//     MovementSystem's velocity projection, which prevents penetration before
//     integration. Forcing a static-dynamic overlap in a test is therefore an
//     out-of-the-ordinary setup that CollisionSystem intentionally ignores.
//
// TextureManager and RenderSystem are NOT unit-tested here because they
// require an active OpenGL context. Run the game to integration-test those.
// ---------------------------------------------------------------------------

// Helper: create an entity with a centered AABB collider.
static entt::entity makeEntity(EntityManager& em, float x, float y, float w, float h,
                               bool is_solid = true, bool dynamic = true)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Collider>(e, Collider{w, h, is_solid});
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

    REQUIRE(em.collision_events.empty());
}

TEST_CASE("Overlapping entities emit a CollisionEvent", "[collision]")
{
    EntityManager em;
    // Two 32x32 boxes centered 10px apart — they overlap by 22px on X.
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    makeEntity(em, 10.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em);

    REQUIRE(em.collision_events.size() == 1);
}

TEST_CASE("Static-vs-dynamic overlap: dynamic entity depenetrated, static never moves",
          "[collision]")
{
    EntityManager em;
    // Wall at (0,0), dynamic player forced at (20,0) — overlapping by 12px on X.
    // In normal gameplay MovementSystem prevents this; here we force the overlap
    // to verify the depenetration pass pushes the dynamic entity out of the static.
    //
    // overlapX = (16+16) - |20-0| = 32-20 = 12. Player pushed right by 12 → x=32.
    auto wall = makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    auto player = makeEntity(em, 20.0f, 0.0f, 32.0f, 32.0f, true, true);

    CollisionSystem::update(em);

    // Static never moves.
    REQUIRE(em.registry().get<Transform>(wall).x == Catch::Approx(0.0f));
    // Dynamic is pushed out of the static to the boundary.
    REQUIRE(em.registry().get<Transform>(player).x == Catch::Approx(32.0f));

    // Event is still emitted (useful for gameplay: combat hits, trigger zones).
    REQUIRE_FALSE(em.collision_events.empty());
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
    REQUIRE(em.collision_events.size() == 1);
}

TEST_CASE("Dynamic-vs-dynamic solid: both pushed apart equally", "[collision]")
{
    EntityManager em;
    // Two dynamic 32x32 boxes: A at x=0, B at x=20 — overlap 12px on X.
    auto a = makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, true);
    auto b = makeEntity(em, 20.0f, 0.0f, 32.0f, 32.0f, true, true);

    CollisionSystem::update(em);

    const auto& ta = em.registry().get<Transform>(a);
    const auto& tb = em.registry().get<Transform>(b);

    // Each pushed 6px in opposite directions (half of the 12px overlap).
    REQUIRE(ta.x == Catch::Approx(-6.0f));
    REQUIRE(tb.x == Catch::Approx(26.0f));

    REQUIRE(em.collision_events.size() == 1);
}

TEST_CASE("Enemy push into wall: dynamic entity depenetrated after dynamic-vs-dynamic resolution",
          "[collision]")
{
    EntityManager em;
    // Regression: enemy overlaps player and CollisionSystem pushes player toward a wall.
    // Without the depenetration pass the player lands inside the wall and becomes
    // permanently stuck (MovementSystem sees a pre-existing overlap → zeroes velocity).
    //
    // Wall at (0,0) static. Player at (32,0) dynamic — touching wall's right face.
    // Enemy at (58,0) dynamic — overlaps player by 6px on X.
    //
    // Dynamic-vs-dynamic: player pushed left by 3px to x=29 (inside wall).
    //                     enemy pushed right by 3px to x=61.
    // Depenetration:      player pushed right by 3px back to x=32 (wall boundary).
    auto wall = makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);   // static
    auto player = makeEntity(em, 32.0f, 0.0f, 32.0f, 32.0f, true, true); // dynamic
    auto enemy = makeEntity(em, 58.0f, 0.0f, 32.0f, 32.0f, true, true);  // dynamic

    CollisionSystem::update(em);

    REQUIRE(em.registry().get<Transform>(wall).x == Catch::Approx(0.0f));
    REQUIRE(em.registry().get<Transform>(player).x == Catch::Approx(32.0f)); // not inside wall
    REQUIRE(em.registry().get<Transform>(enemy).x == Catch::Approx(61.0f));

    // Player-enemy collision event recorded.
    REQUIRE(em.collision_events.size() == 1);
}

TEST_CASE("Collision events are cleared between frames", "[collision]")
{
    EntityManager em;
    makeEntity(em, 0.0f, 0.0f, 32.0f, 32.0f, true, false);
    makeEntity(em, 10.0f, 0.0f, 32.0f, 32.0f, true, false);

    CollisionSystem::update(em); // emits 1 event
    REQUIRE(em.collision_events.size() == 1);

    // Move entities apart so they no longer overlap, then run again.
    auto view = em.registry().view<Transform>();
    int idx = 0;
    for (auto [entity, t] : view.each())
    {
        t.x = static_cast<float>(idx) * 200.0f;
        ++idx;
    }

    CollisionSystem::update(em); // no overlap → events cleared, none added
    REQUIRE(em.collision_events.empty());
}
