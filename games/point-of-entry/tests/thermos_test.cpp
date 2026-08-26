#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/PlayerSystem.h"
#include "systems/ThermosSystem.h"

#include <catch2/catch_test_macros.hpp>

// THE FLASK: a fixed number of sips between rests, and a rest is the only thing that fills it.
// That is the walk back down made into a resource, so it is worth pinning that the count only
// ever goes the way it should.
namespace
{
entt::entity aHurtMan(EntityManager& em)
{
    const entt::entity p = em.registry().create();
    em.registry().emplace<Health>(p, Health{10, 100});
    em.registry().emplace<Stamina>(p, Stamina{10.0f, 100.0f});
    player::bind(p);
    return p;
}
} // namespace

TEST_CASE("a sip heals him and costs a sip", "[thermos]")
{
    EntityManager em;
    const entt::entity p = aHurtMan(em);
    thermos::load("config/stats.json");
    thermos::restore(0, thermos::sipsMax());
    REQUIRE(thermos::sipsLeft() > 0);

    const int before = thermos::sipsLeft();
    REQUIRE(thermos::sip(em));
    CHECK(thermos::sipsLeft() == before - 1);
    CHECK(em.registry().get<Health>(p).current > 10);
}

TEST_CASE("an empty flask does nothing at all", "[thermos]")
{
    EntityManager em;
    const entt::entity p = aHurtMan(em);
    thermos::load("config/stats.json");
    thermos::restore(0, 0);

    CHECK_FALSE(thermos::sip(em));
    CHECK(thermos::sipsLeft() == 0);
    CHECK(em.registry().get<Health>(p).current == 10); // and it did not heal him for free
}

TEST_CASE("a sip never heals past full", "[thermos]")
{
    EntityManager em;
    const entt::entity p = aHurtMan(em);
    em.registry().get<Health>(p).current = 100;
    thermos::load("config/stats.json");
    thermos::restore(0, thermos::sipsMax());

    thermos::sip(em);
    CHECK(em.registry().get<Health>(p).current == 100);
}

TEST_CASE("resting is what fills it, and fills it completely", "[thermos]")
{
    EntityManager em;
    aHurtMan(em);
    thermos::load("config/stats.json");
    thermos::restore(0, 0);
    REQUIRE(thermos::sipsLeft() == 0);

    thermos::rest(em);
    CHECK(thermos::sipsLeft() == thermos::sipsMax());
}
