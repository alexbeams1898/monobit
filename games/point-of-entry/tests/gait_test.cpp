#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/GaitSystem.h"

#include <catch2/catch_test_macros.hpp>

// THE WALK, and the rule that carrying a Gait is what makes a thing a walker. The bug this
// pins was audible before it was visible: the gait claimed everything with a sprite that
// moved -- spray droplets included -- and every droplet crossing a stride fired a footstep,
// which is ninety of them a second.
namespace
{
entt::entity walker(EntityManager& em, float x, float y)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{x, y});
    reg.emplace<PreviousTransform>(e, PreviousTransform{x, y});
    Sprite spr{};
    spr.src_h = 32;
    reg.emplace<Sprite>(e, spr);
    return e;
}

void moveTo(EntityManager& em, entt::entity e, float x, float y)
{
    auto& t = em.registry().get<Transform>(e);
    auto& prev = em.registry().get<PreviousTransform>(e);
    prev.x = t.x;
    prev.y = t.y;
    t.x = x;
    t.y = y;
}
} // namespace

TEST_CASE("only a thing with a gait is walked", "[gait]")
{
    EntityManager em;
    const entt::entity thing = walker(em, 0.0f, 0.0f);
    moveTo(em, thing, 200.0f, 0.0f); // a droplet's worth of travel in one tick

    walk_bob::update(em, 1.0f / 60.0f);

    // No Gait, so the system does not claim it -- and does not rock it, or count a footfall
    // for it, or write a draw offset onto art that has nothing to do with walking.
    CHECK_FALSE(em.registry().all_of<Gait>(thing));
    CHECK(em.registry().get<Sprite>(thing).draw_offset_y == 0.0f);
    CHECK(em.registry().get<Sprite>(thing).rotation == 0.0f);
}

TEST_CASE("a walker's footfalls come off the distance it has walked", "[gait]")
{
    EntityManager em;
    const entt::entity him = walker(em, 0.0f, 0.0f);
    em.registry().emplace<Gait>(him, Gait{});

    // A stride is 34px. Walk a long way in small steps and the count follows the distance.
    float x = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        x += 3.4f;
        moveTo(em, him, x, 0.0f);
        walk_bob::update(em, 1.0f / 60.0f);
    }
    const auto& gait = em.registry().get<Gait>(him);
    CHECK(gait.travelled > 330.0f);
    CHECK(gait.footfalls == static_cast<int>(gait.travelled / 34.0f));
    CHECK(gait.footfalls >= 9); // ten strides of travel, give or take the last part-step
}

TEST_CASE("standing still settles the walk instead of freezing it mid-step", "[gait]")
{
    EntityManager em;
    const entt::entity him = walker(em, 0.0f, 0.0f);
    em.registry().emplace<Gait>(him, Gait{});

    for (int i = 0; i < 20; ++i)
    {
        moveTo(em, him, static_cast<float>(i) * 5.0f, 0.0f);
        walk_bob::update(em, 1.0f / 60.0f);
    }
    CHECK(em.registry().get<Gait>(him).rest == 0.0f); // walking

    const auto& t = em.registry().get<Transform>(him);
    const float stopped = t.x;
    for (int i = 0; i < 60; ++i)
    {
        moveTo(em, him, stopped, 0.0f);
        walk_bob::update(em, 1.0f / 60.0f);
    }
    const auto& gait = em.registry().get<Gait>(him);
    CHECK(gait.rest == 1.0f);      // fully settled
    CHECK(gait.travelled == 0.0f); // and the stride clock is back to nothing
    CHECK(em.registry().get<Sprite>(him).draw_offset_y == 0.0f); // stood level, not mid-hop
}
