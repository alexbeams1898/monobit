#include "ConfigLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// ConfigLoader tests — no window, no GPU required.
//
// These tests read real JSON files from config/entities/.
// CTest runs them with WORKING_DIRECTORY set to the project root so that
// relative paths like "config/entities/player.json" resolve correctly.
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader loads correctional_officer with correct components", "[config]")
{
    EntityManager em;
    auto entity = ConfigLoader::loadEntity(em, "config/entities/correctional_officer.json");

    REQUIRE(em.registry().valid(entity));
    REQUIRE(em.registry().all_of<Tag, Transform, Health, Velocity, Collider, Sprite, AIController>(
        entity));

    auto& tag = em.registry().get<Tag>(entity);
    REQUIRE(tag.name == "correctional_officer");

    auto& health = em.registry().get<Health>(entity);
    REQUIRE(health.current == 60);
    REQUIRE(health.max == 60);

    auto& collider = em.registry().get<Collider>(entity);
    REQUIRE(collider.width == 32.0f);
    REQUIRE(collider.isSolid);

    auto& ai = em.registry().get<AIController>(entity);
    REQUIRE(ai.state == AIController::State::Chase);
    REQUIRE(ai.speed == Catch::Approx(80.0f));
}

TEST_CASE("ConfigLoader loads player entity with correct values", "[config]")
{
    EntityManager em;
    auto entity = ConfigLoader::loadEntity(em, "config/entities/player.json");

    REQUIRE(em.registry().valid(entity));

    auto& tag = em.registry().get<Tag>(entity);
    REQUIRE(tag.name == "player");

    auto& transform = em.registry().get<Transform>(entity);
    REQUIRE(transform.x == 640.0f);
    REQUIRE(transform.y == 360.0f);

    auto& health = em.registry().get<Health>(entity);
    REQUIRE(health.max == 150);
}

TEST_CASE("ConfigLoader returns invalid entity for missing file", "[config]")
{
    EntityManager em;
    auto entity = ConfigLoader::loadEntity(em, "config/entities/nonexistent.json");
    // Can't use REQUIRE(entity == entt::null) — ambiguous operator== between
    // Catch2 and entt. Check validity instead, which is the meaningful property.
    REQUIRE_FALSE(em.registry().valid(entity));
    REQUIRE(em.registry().view<Tag>().size() == 0);
}
