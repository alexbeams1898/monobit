#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/AnimationSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

// Helper: build a minimal animated entity with test animation data.
static entt::entity makeAnimatedEntity(EntityManager& em, AnimState initialState = AnimState::Idle)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e);
    em.registry().emplace<Velocity>(e);

    FacingDirection facing;
    facing.dx = 1.0f;
    facing.dy = 0.0f;
    facing.render_dx = 1.0f;
    facing.render_dy = 0.0f;
    em.registry().emplace<FacingDirection>(e, facing);

    Sprite spr;
    spr.src_w = 32;
    spr.src_h = 32;
    em.registry().emplace<Sprite>(e, spr);

    Animation anim;
    anim.state = initialState;
    anim.frame_width = 32;
    anim.frame_height = 32;
    anim.max_frames_per_state = 8; // max across all states (run has 8)

    // Idle: 1 frame, static
    anim.states[static_cast<int>(AnimState::Idle)] = {0, 1, 0.0f};
    // Walk: 4 frames, 0.1s each
    anim.states[static_cast<int>(AnimState::Walk)] = {1, 4, 0.1f};
    // Attack: 3 frames, 0.08s each
    anim.states[static_cast<int>(AnimState::Attack)] = {2, 3, 0.08f};
    // Hit: 2 frames, 0.1s each
    anim.states[static_cast<int>(AnimState::Hit)] = {3, 2, 0.1f};
    // Death: 5 frames, 0.12s each
    anim.states[static_cast<int>(AnimState::Death)] = {4, 5, 0.12f};
    // Run: 8 frames, 0.08s each
    anim.states[static_cast<int>(AnimState::Run)] = {5, 8, 0.08f};

    em.registry().emplace<Animation>(e, anim);
    return e;
}

// ---------------------------------------------------------------------------
// Direction snapping
// ---------------------------------------------------------------------------

TEST_CASE("Cardinal direction snapping - right is East", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = 1.0f;
    facing.render_dy = 0.0f;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::East);
}

TEST_CASE("Cardinal direction snapping - left is West", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = -1.0f;
    facing.render_dy = 0.0f;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::West);
}

TEST_CASE("Cardinal direction snapping - down is South", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = 0.0f;
    facing.render_dy = 1.0f;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::South);
}

TEST_CASE("Cardinal direction snapping - up is North", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = 0.0f;
    facing.render_dy = -1.0f;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::North);
}

TEST_CASE("Cardinal direction snapping - diagonal goes to dominant axis", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);

    // More horizontal than vertical -> East
    facing.render_dx = 0.9f;
    facing.render_dy = 0.3f;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::East);
}

TEST_CASE("Cardinal direction snapping - exact 45 degrees goes vertical", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);
    auto& facing = em.registry().get<FacingDirection>(e);

    // Tie: abs(dx) == abs(dy), vertical wins -> South
    facing.render_dx = 0.707f;
    facing.render_dy = 0.707f;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).dir == CardinalDir::South);
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

TEST_CASE("Walk state advances frames when set externally", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Walk: 4 frames, 0.1s each. After 0.1s, should advance.
    AnimationSystem::update(em, 0.1f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Walk);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 1);
}

TEST_CASE("Idle state stays at frame 0 (single-frame state)", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Idle);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

TEST_CASE("State transition - Attack state is preserved from external setter", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Attack);

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Attack);
}

TEST_CASE("State transition - Hit state is preserved from external setter", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Hit);

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Hit);
}

TEST_CASE("Death state is preserved from external setter", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Death);

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Death);
}

TEST_CASE("State change resets frame when switching to Death", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Advance a few frames in Walk.
    AnimationSystem::update(em, 0.2f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 2);

    // External state setter switches to Death.
    em.registry().get<Animation>(e).state = AnimState::Death;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Death);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

TEST_CASE("State change resets frame when switching to Hit", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    AnimationSystem::update(em, 0.2f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 2);

    // External state setter switches to Hit.
    em.registry().get<Animation>(e).state = AnimState::Hit;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Hit);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

// ---------------------------------------------------------------------------
// Frame advancement
// ---------------------------------------------------------------------------

TEST_CASE("Frame advances after duration elapses", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Walk: 4 frames, 0.1s each. After 0.1s, frame should advance to 1.
    AnimationSystem::update(em, 0.1f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 1);
}

TEST_CASE("Frame loops on non-terminal animations", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Walk: 4 frames, 0.1s each. After 0.4s, should loop back to 0.
    AnimationSystem::update(em, 0.4f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

TEST_CASE("Death animation holds last frame", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Death);

    // Death: 5 frames, 0.12s each. Total = 0.6s. After 1.0s, should be on frame 4.
    AnimationSystem::update(em, 1.0f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Death);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 4);
}

TEST_CASE("State change resets frame index and timer", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Advance to frame 2.
    AnimationSystem::update(em, 0.2f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 2);

    // External setter transitions to Idle.
    em.registry().get<Animation>(e).state = AnimState::Idle;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).state == AnimState::Idle);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
    REQUIRE(em.registry().get<Animation>(e).frame_timer == Catch::Approx(0.0f).margin(0.001f));
}

// ---------------------------------------------------------------------------
// UV rect calculation
// ---------------------------------------------------------------------------

TEST_CASE("Sprite src rect matches expected position for East Walk frame 0", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Default facing is East (render_dx=1, render_dy=0).
    // East = dir index 2. max_frames=8. col = 2*8 + 0 = 16.
    // Walk row = 1. src_x = 16 * 32 = 512, src_y = 1 * 32 = 32.
    AnimationSystem::update(em, 0.001f);

    const auto& spr = em.registry().get<Sprite>(e);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.dir == CardinalDir::East);
    REQUIRE(spr.src_x == 512);
    REQUIRE(spr.src_y == 32);
    REQUIRE(spr.src_w == 32);
    REQUIRE(spr.src_h == 32);
}

TEST_CASE("Sprite src rect for South Idle", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);

    // Set facing to South.
    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = 0.0f;
    facing.render_dy = 1.0f;

    // South = dir index 0. max_frames=8. col = 0*8 + 0 = 0.
    // Idle row = 0. src_x = 0, src_y = 0.
    AnimationSystem::update(em, 0.016f);

    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 0);
    REQUIRE(spr.src_y == 0);
}

TEST_CASE("Sprite src rect advances column with frame index", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Advance to frame 2.
    AnimationSystem::update(em, 0.2f);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.frame_index == 2);

    // East = dir 2, max_frames=8, col = 2*8 + 2 = 18.
    // src_x = 18 * 32 = 576. Walk row = 1, src_y = 32.
    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 576);
    REQUIRE(spr.src_y == 32);
}

// ---------------------------------------------------------------------------
// Entities without Animation remain unaffected
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Entities without Animation remain unaffected
// ---------------------------------------------------------------------------

TEST_CASE("Non-animated entities are not touched by AnimationSystem", "[animation]")
{
    EntityManager em;
    auto e = em.create();
    Sprite spr;
    spr.src_x = 42;
    spr.src_y = 99;
    spr.src_w = 32;
    spr.src_h = 32;
    em.registry().emplace<Sprite>(e, spr);
    em.registry().emplace<Transform>(e);

    AnimationSystem::update(em, 0.016f);

    // Should be unchanged since there's no Animation component.
    REQUIRE(em.registry().get<Sprite>(e).src_x == 42);
    REQUIRE(em.registry().get<Sprite>(e).src_y == 99);
}
