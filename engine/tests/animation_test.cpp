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
    anim.max_frames_per_state = 5; // max across all states (death has 5)

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
    // East = dir index 2. max_frames=5. col = 2*5 + 0 = 10.
    // Walk row = 1. src_x = 10 * 32 = 320, src_y = 1 * 32 = 32.
    AnimationSystem::update(em, 0.001f);

    const auto& spr = em.registry().get<Sprite>(e);
    const auto& anim = em.registry().get<Animation>(e);
    REQUIRE(anim.dir == CardinalDir::East);
    REQUIRE(spr.src_x == 320);
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

    // South = dir index 0. max_frames=5. col = 0*5 + 0 = 0.
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

    // East = dir 2, max_frames=5, col = 2*5 + 2 = 12.
    // src_x = 12 * 32 = 384. Walk row = 1, src_y = 32.
    const auto& spr = em.registry().get<Sprite>(e);
    REQUIRE(spr.src_x == 384);
    REQUIRE(spr.src_y == 32);
}

// ---------------------------------------------------------------------------
// Entities without Animation remain unaffected
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Body-part state resolution
// ---------------------------------------------------------------------------

// Helper: create a parent entity with gameplay components but no Sprite/Animation,
// plus a body-part child with Sprite+Animation linked to the parent.
static entt::entity makeBodyPartChild(EntityManager& em, entt::entity parent,
                                      bool direction_from_facing)
{
    auto child = em.create();

    BodyPart bp;
    bp.parent = parent;
    bp.direction_from_facing = direction_from_facing;
    em.registry().emplace<BodyPart>(child, bp);
    em.registry().emplace<Transform>(child);

    Sprite spr;
    spr.src_w = 32;
    spr.src_h = 32;
    em.registry().emplace<Sprite>(child, spr);

    Animation anim;
    anim.frame_width = 32;
    anim.frame_height = 32;
    anim.max_frames_per_state = 5;
    anim.states[static_cast<int>(AnimState::Idle)] = {0, 1, 0.0f};
    anim.states[static_cast<int>(AnimState::Walk)] = {1, 4, 0.1f};
    anim.states[static_cast<int>(AnimState::Attack)] = {2, 3, 0.08f};
    anim.states[static_cast<int>(AnimState::Hit)] = {3, 2, 0.1f};
    anim.states[static_cast<int>(AnimState::Death)] = {4, 5, 0.12f};
    em.registry().emplace<Animation>(child, anim);

    return child;
}

static entt::entity makeParentEntity(EntityManager& em)
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
    return e;
}

TEST_CASE("Lower body direction follows parent velocity", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    em.registry().get<Animation>(lower).state = AnimState::Walk;
    em.registry().get<Velocity>(parent).dy = -100.0f; // moving north

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(lower).dir == CardinalDir::North);
}

TEST_CASE("Upper body preserves Attack state set externally", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto upper = makeBodyPartChild(em, parent, true);
    em.registry().get<Animation>(upper).state = AnimState::Attack;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(upper).state == AnimState::Attack);
}

TEST_CASE("Lower body preserves Walk state independently of upper", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    auto upper = makeBodyPartChild(em, parent, true);
    em.registry().get<Animation>(lower).state = AnimState::Walk;
    em.registry().get<Animation>(upper).state = AnimState::Attack;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(lower).state == AnimState::Walk);
    REQUIRE(em.registry().get<Animation>(upper).state == AnimState::Attack);
}

TEST_CASE("Upper body ignores Velocity - stays Idle", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto upper = makeBodyPartChild(em, parent, true);
    em.registry().get<Velocity>(parent).dx = 100.0f;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(upper).state == AnimState::Idle);
}

TEST_CASE("Both body parts advance Death state set externally", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    auto upper = makeBodyPartChild(em, parent, true);
    em.registry().get<Animation>(lower).state = AnimState::Death;
    em.registry().get<Animation>(upper).state = AnimState::Death;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(lower).state == AnimState::Death);
    REQUIRE(em.registry().get<Animation>(upper).state == AnimState::Death);
}

TEST_CASE("Both body parts advance Hit state set externally", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    auto upper = makeBodyPartChild(em, parent, true);
    em.registry().get<Animation>(lower).state = AnimState::Hit;
    em.registry().get<Animation>(upper).state = AnimState::Hit;

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(lower).state == AnimState::Hit);
    REQUIRE(em.registry().get<Animation>(upper).state == AnimState::Hit);
}

TEST_CASE("Lower body direction from parent Velocity", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    em.registry().get<Velocity>(parent).dx = 0.0f;
    em.registry().get<Velocity>(parent).dy = -100.0f; // moving north

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(lower).dir == CardinalDir::North);
}

TEST_CASE("Upper body direction from parent FacingDirection", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto upper = makeBodyPartChild(em, parent, true);
    auto& facing = em.registry().get<FacingDirection>(parent);
    facing.render_dx = -1.0f;
    facing.render_dy = 0.0f; // aiming west

    AnimationSystem::update(em, 0.016f);
    REQUIRE(em.registry().get<Animation>(upper).dir == CardinalDir::West);
}

TEST_CASE("Death cascade destroys body-part children", "[animation][bodypart]")
{
    EntityManager em;
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);
    auto upper = makeBodyPartChild(em, parent, true);

    // Cascade destroy children then parent (same pattern as DeathSystem).
    std::vector<entt::entity> children;
    for (auto [child, bp] : em.registry().view<BodyPart>().each())
        if (bp.parent == parent)
            children.push_back(child);
    for (auto child : children)
        em.destroy(child);
    em.destroy(parent);

    REQUIRE_FALSE(em.registry().valid(parent));
    REQUIRE_FALSE(em.registry().valid(lower));
    REQUIRE_FALSE(em.registry().valid(upper));
}

TEST_CASE("Standalone entities unaffected by body-part logic", "[animation][bodypart]")
{
    EntityManager em;
    auto standalone = makeAnimatedEntity(em, AnimState::Walk);
    em.registry().get<Velocity>(standalone).dx = 100.0f;

    // Also create a body-part setup in the same registry.
    auto parent = makeParentEntity(em);
    auto lower = makeBodyPartChild(em, parent, false);

    AnimationSystem::update(em, 0.016f);

    // Standalone keeps its externally-set Walk state.
    REQUIRE(em.registry().get<Animation>(standalone).state == AnimState::Walk);
    // Body-part child keeps its default Idle state.
    REQUIRE(em.registry().get<Animation>(lower).state == AnimState::Idle);
}

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
