#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/ParticleSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// ParticleSystem tests -- no window, no GPU required.
// ---------------------------------------------------------------------------

TEST_CASE("Particle ages and is destroyed when lifetime expires", "[particle]")
{
    EntityManager em;
    auto& reg = em.registry();

    auto e = reg.create();
    reg.emplace<Transform>(e, 100.0f, 100.0f, 1.0f);
    Particle p;
    p.lifetime = 1.0f;
    p.age = 0.0f;
    reg.emplace<Particle>(e, p);

    // Tick halfway -- entity still alive.
    ParticleSystem::update(em, 0.5);
    REQUIRE(reg.valid(e));
    REQUIRE(reg.get<Particle>(e).age == Catch::Approx(0.5f));

    // Tick past lifetime -- entity destroyed.
    ParticleSystem::update(em, 0.6);
    REQUIRE_FALSE(reg.valid(e));
}

TEST_CASE("Particle scale interpolates over lifetime", "[particle]")
{
    EntityManager em;
    auto& reg = em.registry();

    auto e = reg.create();
    reg.emplace<Transform>(e, 0.0f, 0.0f, 1.0f);
    Particle p;
    p.lifetime = 1.0f;
    p.age = 0.0f;
    p.start_scale = 2.0f;
    p.end_scale = 0.0f;
    reg.emplace<Particle>(e, p);

    // Halfway through lifetime: scale should be midpoint (1.0).
    ParticleSystem::update(em, 0.5);
    REQUIRE(reg.get<Transform>(e).scale == Catch::Approx(1.0f));
}

TEST_CASE("spawnEmberBurst creates correct number of entities", "[particle]")
{
    EntityManager em;
    ParticleSystem::spawnEmberBurst(em, 200.0f, 200.0f, 5);

    int count = 0;
    em.registry().view<Particle>().each([&](const Particle&) { ++count; });
    REQUIRE(count == 5);
}

TEST_CASE("Ember particles have correct components and no collider", "[particle]")
{
    EntityManager em;
    ParticleSystem::spawnEmberBurst(em, 100.0f, 100.0f, 1);

    auto view = em.registry().view<Particle>();
    REQUIRE(view.size() == 1);

    auto entity = *view.begin();
    REQUIRE(em.registry().all_of<Transform>(entity));
    REQUIRE(em.registry().all_of<Velocity>(entity));
    REQUIRE(em.registry().all_of<Sprite>(entity));
    REQUIRE(em.registry().all_of<Tag>(entity));
    REQUIRE(em.registry().get<Tag>(entity).name == "ember");
    REQUIRE_FALSE(em.registry().all_of<Collider>(entity));
}

TEST_CASE("Ember particles drift upward", "[particle]")
{
    EntityManager em;
    ParticleSystem::spawnEmberBurst(em, 100.0f, 100.0f, 3);

    for (auto [entity, vel] : em.registry().view<Velocity>().each())
    {
        REQUIRE(vel.dy < 0.0f); // negative Y = upward
    }
}
