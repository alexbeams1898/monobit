#include "systems/SpriteAnimSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/AnimationSystem.h"

#include <catch2/catch_test_macros.hpp>

// A creature set up the way the swarm builds one: sprite, facing, and a tagged animation
// attached from a def. The promise: the engine actually advances its frames, and mirrored
// facing keeps working with the Animation component present.

namespace
{
sprite_def::Def twoFrameDef()
{
    sprite_def::Def def;
    def.ok = true;
    def.sheet = "assets/sprites/ant.png";
    def.frame_w = 7;
    def.frame_h = 4;
    def.frames = 2;
    def.durations_ms = {100, 100};
    def.anims.push_back(sprite_def::Anim{"walk", 0, 1});
    return def;
}
} // namespace

TEST_CASE("an attached walk tag advances frames", "[sprite_anim]")
{
    EntityManager em;
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e);
    reg.emplace<Sprite>(e);
    reg.emplace<FacingDirection>(e);
    sprite_anim::attach(em, e, twoFrameDef());

    REQUIRE(sprite_anim::current(em, e) == "walk");

    // Tick past one frame duration: the source rect must move to the second frame.
    AnimationSystem::update(em, 0.06f);
    const int first = reg.get<Sprite>(e).src_x;
    AnimationSystem::update(em, 0.06f);
    const int second = reg.get<Sprite>(e).src_x;
    CHECK(first != second);

    // And back: a 2-frame loop alternates.
    AnimationSystem::update(em, 0.1f);
    CHECK(reg.get<Sprite>(e).src_x == first);
}

TEST_CASE("mirrored facing still flips an animated creature", "[sprite_anim]")
{
    EntityManager em;
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e);
    reg.emplace<Sprite>(e);
    auto& facing = reg.emplace<FacingDirection>(e);
    sprite_anim::attach(em, e, twoFrameDef());

    facing.render_dx = -1.0f;
    AnimationSystem::update(em, 0.01f);
    CHECK(reg.get<Sprite>(e).flip_x);

    facing.render_dx = 1.0f;
    AnimationSystem::update(em, 0.01f);
    CHECK_FALSE(reg.get<Sprite>(e).flip_x);
}
