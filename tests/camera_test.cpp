#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CameraSystem.h"
#include "systems/MovementSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// CameraSystem tests — no window, no GPU, no SDL required.
//
// TextureManager and RenderSystem are NOT unit-tested here because they
// require a live OpenGL context (GPU). They are covered by running the game
// (integration test).
// ---------------------------------------------------------------------------

TEST_CASE("CameraSystem snaps camera to player Transform", "[camera]")
{
    EntityManager em;
    auto player = em.create();
    em.registry().emplace<Transform>(player, Transform{300.0f, 400.0f});
    em.registry().emplace<Camera>(player);
    em.registry().emplace<Input>(player);

    CameraSystem::update(em);

    auto& cam = em.registry().get<Camera>(player);
    REQUIRE(cam.x == Catch::Approx(300.0f));
    REQUIRE(cam.y == Catch::Approx(400.0f));
}

TEST_CASE("CameraSystem tracks updated Transform position", "[camera]")
{
    EntityManager em;
    auto player = em.create();
    em.registry().emplace<Transform>(player, Transform{0.0f, 0.0f});
    em.registry().emplace<Camera>(player);
    em.registry().emplace<Input>(player);

    CameraSystem::update(em);

    em.registry().patch<Transform>(player, [](Transform& t) {
        t.x = 640.0f;
        t.y = 360.0f;
    });

    CameraSystem::update(em);

    auto& cam = em.registry().get<Camera>(player);
    REQUIRE(cam.x == Catch::Approx(640.0f));
    REQUIRE(cam.y == Catch::Approx(360.0f));
}

TEST_CASE("CameraSystem does not update inactive Camera", "[camera]")
{
    EntityManager em;
    auto player = em.create();
    em.registry().emplace<Transform>(player, Transform{500.0f, 500.0f});
    em.registry().emplace<Camera>(player, Camera{0.0f, 0.0f, false}); // active = false
    em.registry().emplace<Input>(player);

    CameraSystem::update(em);

    auto& cam = em.registry().get<Camera>(player);
    REQUIRE(cam.x == Catch::Approx(0.0f));
    REQUIRE(cam.y == Catch::Approx(0.0f));
}

TEST_CASE("Camera tracks player position after movement in the same frame", "[camera]")
{
    // Regression test for update-order bug: CameraSystem must run AFTER
    // MovementSystem each frame, or the camera lags one frame behind the player
    // and the sprite visually drifts before snapping back each frame.
    EntityManager em;
    auto player = em.create();
    em.registry().emplace<Transform>(player, Transform{0.0f, 0.0f});
    em.registry().emplace<Velocity>(player, Velocity{200.0f, 0.0f}); // moving right
    em.registry().emplace<Input>(player);
    em.registry().emplace<Camera>(player);

    // Correct order: movement first, then camera.
    constexpr double dt = 1.0 / 60.0;
    MovementSystem::update(em, dt);
    CameraSystem::update(em);

    const auto& t   = em.registry().get<Transform>(player);
    const auto& cam = em.registry().get<Camera>(player);

    // Camera must match the player's new position, not the old one.
    REQUIRE(cam.x == Catch::Approx(t.x));
    REQUIRE(cam.y == Catch::Approx(t.y));
}

TEST_CASE("CameraSystem only tracks entity with Input component", "[camera]")
{
    // An entity with Transform + Camera but no Input (e.g. a cutscene cam)
    // should not be updated by CameraSystem — which requires all three.
    EntityManager em;
    auto cam_entity = em.create();
    em.registry().emplace<Transform>(cam_entity, Transform{999.0f, 999.0f});
    em.registry().emplace<Camera>(cam_entity); // no Input

    CameraSystem::update(em);

    auto& cam = em.registry().get<Camera>(cam_entity);
    REQUIRE(cam.x == Catch::Approx(0.0f));
    REQUIRE(cam.y == Catch::Approx(0.0f));
}
