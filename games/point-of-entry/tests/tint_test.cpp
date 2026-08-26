#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/PlayerSystem.h"
#include "systems/TintSystem.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// THE POINT OF THE PASS is that a tint cannot outlive its reason: it is decided from scratch
// every tick, so there is no state to leave behind and nothing has to remember to undo itself.
namespace
{
entt::entity aBody(EntityManager& em, int current, int max)
{
    const entt::entity e = em.registry().create();
    em.registry().emplace<Health>(e, Health{current, max});
    return e;
}
} // namespace

TEST_CASE("a wound shows in the colour, and healing takes it back out", "[tint]")
{
    REQUIRE(tint::load("config/stats.json"));
    EntityManager em;
    const entt::entity him = aBody(em, 100, 100);
    player::bind(him);

    SECTION("unhurt, and above the threshold, he is his own colour")
    {
        tint::update(em);
        CHECK_FALSE(em.registry().all_of<TintOverride>(him));

        em.registry().get<Health>(him).current = 70; // 70%, still above wound_from
        tint::update(em);
        CHECK_FALSE(em.registry().all_of<TintOverride>(him));
    }

    SECTION("the further down he goes the redder he gets")
    {
        em.registry().get<Health>(him).current = 40;
        tint::update(em);
        REQUIRE(em.registry().all_of<TintOverride>(him));
        const float atForty = em.registry().get<TintOverride>(him).g;

        em.registry().get<Health>(him).current = 10;
        tint::update(em);
        const float atTen = em.registry().get<TintOverride>(him).g;
        CHECK(atTen < atForty); // less green left = more red
        CHECK(atTen >= 0.0f);
    }

    SECTION("healing above the threshold clears it, with nothing having to undo anything")
    {
        em.registry().get<Health>(him).current = 10;
        tint::update(em);
        REQUIRE(em.registry().all_of<TintOverride>(him));

        em.registry().get<Health>(him).current = 100;
        tint::update(em);
        CHECK_FALSE(em.registry().all_of<TintOverride>(him));
    }

    SECTION("a body one frame past dead is still a colour that can be drawn")
    {
        em.registry().get<Health>(him).current = -5;
        tint::update(em);
        REQUIRE(em.registry().all_of<TintOverride>(him));
        const auto& t = em.registry().get<TintOverride>(him);
        CHECK(t.g >= 0.0f);
        CHECK(t.b >= 0.0f);
    }
}

TEST_CASE("being struck outranks being wounded", "[tint]")
{
    REQUIRE(tint::load("config/stats.json"));
    EntityManager em;
    const entt::entity him = aBody(em, 5, 100); // deep in the red
    player::bind(him);
    em.registry().emplace<HitFlash>(him, HitFlash{tint::flashSeconds(false)});

    tint::update(em);
    REQUIRE(em.registry().all_of<TintOverride>(him));
    const auto& lit = em.registry().get<TintOverride>(him);
    // The flash is white and blown out; the wound would have pulled green and blue down.
    CHECK(lit.g > 1.0f);
    CHECK(lit.g == Catch::Approx(lit.b));

    // And when the flash is gone the wound is showing again -- neither state was ever stored.
    em.registry().remove<HitFlash>(him);
    tint::update(em);
    CHECK(em.registry().get<TintOverride>(him).g < 1.0f);
}

TEST_CASE("anything struck flashes, not only him", "[tint]")
{
    REQUIRE(tint::load("config/stats.json"));
    EntityManager em;
    const entt::entity pest = aBody(em, 3, 10);
    player::bind(entt::null);
    em.registry().emplace<HitFlash>(pest, HitFlash{tint::flashSeconds(false)});

    tint::update(em);
    CHECK(em.registry().all_of<TintOverride>(pest));

    em.registry().remove<HitFlash>(pest);
    tint::update(em);
    // Wounded, but a pest is not who the wound colour is for -- and nothing lingers.
    CHECK_FALSE(em.registry().all_of<TintOverride>(pest));
}
