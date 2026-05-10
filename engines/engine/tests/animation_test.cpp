#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/AnimationSystem.h"

#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Helper: build a minimal animated entity with given row config.
static entt::entity makeAnimatedEntity(EntityManager& em, int row = 0, int frames = 1,
                                       float duration = 0.0f, bool freeze = false)
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
    anim.frame_width = 32;
    anim.frame_height = 32;
    anim.max_frames_per_state = 8;

    anim.current_row = row;
    anim.current_frames = frames;
    anim.current_duration = duration;
    anim.freeze_on_last = freeze;

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
// Row change detection
// ---------------------------------------------------------------------------

TEST_CASE("Row change resets frame index and timer", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Advance to frame 2.
    AnimationSystem::update(em, 0.2f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 2);

    // Switch to row 0 (static).
    auto& anim = em.registry().get<Animation>(e);
    anim.current_row = 0;
    anim.current_frames = 1;
    anim.current_duration = 0.0f;
    AnimationSystem::update(em, 0.016f);
    REQUIRE(anim.frame_index == 0);
    REQUIRE(anim.frame_timer == Catch::Approx(0.0f).margin(0.001f));
}

// ---------------------------------------------------------------------------
// Frame advancement
// ---------------------------------------------------------------------------

TEST_CASE("Frame advances after duration elapses", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    em.registry().get<Velocity>(e).dx = 100.0f;

    AnimationSystem::update(em, 0.1f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 1);
}

TEST_CASE("Frame loops on non-freeze animations", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // 4 frames * 0.1s = 0.4s full loop -> back to 0.
    AnimationSystem::update(em, 0.4f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

TEST_CASE("Freeze animation holds last frame", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 4, 5, 0.12f, true);

    // Total = 0.6s. After 1.0s, should be on frame 4 (last).
    AnimationSystem::update(em, 1.0f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 4);
}

TEST_CASE("Static row stays at frame 0", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(e).frame_index == 0);
}

TEST_CASE("Reverse playback decrements frame index", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    auto& anim = em.registry().get<Animation>(e);
    anim.reverse = true;

    // Reverse from frame 0: (0 - 1 + 4) % 4 = 3.
    AnimationSystem::update(em, 0.1f);
    REQUIRE(anim.frame_index == 3);
}

TEST_CASE("Speed multiplier scales frame duration", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    auto& anim = em.registry().get<Animation>(e);
    anim.speed_multiplier = 2.0f; // 2x slower: effective duration = 0.2s

    // After 0.1s at 2x multiplier, should NOT have advanced yet.
    AnimationSystem::update(em, 0.1f);
    REQUIRE(anim.frame_index == 0);

    // After 0.2s total, should advance.
    AnimationSystem::update(em, 0.1f);
    REQUIRE(anim.frame_index == 1);
}

// ---------------------------------------------------------------------------
// UV rect calculation
// ---------------------------------------------------------------------------

TEST_CASE("Sprite src rect matches expected position for East row 1 frame 0", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Default facing is East (render_dx=1, render_dy=0).
    // East = dir index 2. max_frames=8. col = 2*8 + 0 = 16.
    // Row 1. src_x = 16 * 32 = 512, src_y = 1 * 32 = 32.
    AnimationSystem::update(em, 0.001f);

    const auto& spr = em.registry().get<Sprite>(e);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.dir == CardinalDir::East);
    REQUIRE(spr.src_x == 512);
    REQUIRE(spr.src_y == 32);
    REQUIRE(spr.src_w == 32);
    REQUIRE(spr.src_h == 32);
}

TEST_CASE("Sprite src rect for South row 0", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em);

    auto& facing = em.registry().get<FacingDirection>(e);
    facing.render_dx = 0.0f;
    facing.render_dy = 1.0f;

    AnimationSystem::update(em, 0.016f);

    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 0);
    REQUIRE(spr.src_y == 0);
}

TEST_CASE("Sprite src rect advances column with frame index", "[animation]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);
    em.registry().get<Velocity>(e).dx = 100.0f;

    // Advance to frame 2.
    AnimationSystem::update(em, 0.2f);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.frame_index == 2);

    // East = dir 2, max_frames=8, col = 2*8 + 2 = 18.
    // src_x = 18 * 32 = 576. Row 1, src_y = 32.
    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 576);
    REQUIRE(spr.src_y == 32);
}

// ---------------------------------------------------------------------------
// Non-animated entities remain unaffected
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

    REQUIRE(em.registry().get<Sprite>(e).src_x == 42);
    REQUIRE(em.registry().get<Sprite>(e).src_y == 99);
}

// ---------------------------------------------------------------------------
// Frame mask (per-frame column remap)
// ---------------------------------------------------------------------------

TEST_CASE("Frame mask remaps visible column", "[animation][frame_mask]")
{
    EntityManager em;
    // row 7 (shoot), 13 frames, 0.06s per frame
    auto e = makeAnimatedEntity(em, 7, 13, 0.06f);
    auto& anim = em.registry().get<Animation>(e);
    anim.max_frames_per_state = 13;
    // Pistol mask: [0, 1, 2, 3, 10, 3, 2, 1]
    anim.frame_mask = {0, 1, 2, 3, 10, 3, 2, 1};

    // Default facing is East (render_dx=1). East = dir 2, max_frames=13.
    // Advance 0.25s (> 4 * 0.06s) to deterministically land on frame_index == 4.
    // frame_mask[4] = 10. col = 2*13 + 10 = 36. src_x = 36*32 = 1152.
    AnimationSystem::update(em, 0.25f);
    REQUIRE(anim.frame_index == 4);

    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 1152);
    REQUIRE(spr.src_y == 7 * 32);
}

TEST_CASE("Frame mask controls playback length", "[animation][frame_mask]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 7, 13, 0.1f);
    auto& anim = em.registry().get<Animation>(e);
    anim.max_frames_per_state = 13;
    anim.frame_mask = {0, 1, 2}; // only 3 frames

    // Advance 3 frames (0.3s) -> wraps to frame 0 (3 % 3 = 0).
    AnimationSystem::update(em, 0.3f);
    REQUIRE(anim.frame_index == 0);
}

TEST_CASE("Empty frame mask plays columns 0..N-1 normally", "[animation][frame_mask]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 4, 0.1f);

    // No frame mask set -- default behavior.
    AnimationSystem::update(em, 0.2f);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.frame_index == 2);

    // East = dir 2, max_frames=8, col = 2*8 + 2 = 18. src_x = 18*32 = 576.
    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 576);
}

TEST_CASE("Frame mask clamps frame_index when mask shrinks", "[animation][frame_mask]")
{
    EntityManager em;
    auto e = makeAnimatedEntity(em, 1, 8, 0.1f);
    auto& anim = em.registry().get<Animation>(e);

    // Advance to frame 5.
    AnimationSystem::update(em, 0.5f);
    REQUIRE(anim.frame_index == 5);

    // Apply a 3-element mask. frame_index 5 >= 3, should clamp to 2.
    anim.frame_mask = {0, 2, 4};
    AnimationSystem::update(em, 0.001f);
    REQUIRE(anim.frame_index == 2);
}
