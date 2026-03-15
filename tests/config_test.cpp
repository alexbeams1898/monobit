#include "ConfigLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// ConfigLoader tests — no window, no GPU required.
//
// These tests read real JSON files from config/entities/.
// CTest runs them with WORKING_DIRECTORY set to the project root so that
// relative paths like "config/entities/guard.json" resolve correctly.
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader loads guard entity with correct components", "[config]")
{
    EntityManager em;
    auto entity = ConfigLoader::loadEntity(em, "config/entities/guard.json");

    REQUIRE(em.registry().valid(entity));
    REQUIRE(em.registry().all_of<Tag, Transform, Health, Velocity, Collider, Sprite>(entity));

    auto& tag = em.registry().get<Tag>(entity);
    REQUIRE(tag.name == "guard");

    auto& health = em.registry().get<Health>(entity);
    REQUIRE(health.current == 100);
    REQUIRE(health.max == 100);

    auto& collider = em.registry().get<Collider>(entity);
    REQUIRE(collider.width == 32.0f);
    REQUIRE(collider.isSolid);
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
