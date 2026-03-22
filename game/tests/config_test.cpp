#include "ConfigLoader.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/LevelingSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// ConfigLoader tests — no window, no GPU required.
//
// These tests read real JSON files from config/entities/.
// CTest runs them with WORKING_DIRECTORY set to the project root so that
// relative paths like "config/entities/player.json" resolve correctly.
// ---------------------------------------------------------------------------

TEST_CASE("ConfigLoader loads skeleton entity with correct components", "[config]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto entity = ConfigLoader::loadEntity(em, "config/entities/skeleton.json");

    REQUIRE(em.registry().valid(entity));
    // Health is derived by LevelingSystem — not present until applyInitialDerivations.
    REQUIRE(em.registry()
                .all_of<Tag, Transform, Stats, Weapon, Velocity, Collider, Sprite, AIController>(
                    entity));
    REQUIRE_FALSE(em.registry().all_of<Health>(entity));

    LevelingSystem::applyInitialDerivations(em);

    // skeleton: base_hp=5, end=1 → maxHP = 5 + floor(30 * ln(2)) = 25
    auto& health = em.registry().get<Health>(entity);
    REQUIRE(health.max == 25);
    REQUIRE(health.current == health.max);

    auto& tag = em.registry().get<Tag>(entity);
    REQUIRE(tag.name == "skeleton");

    auto& collider = em.registry().get<Collider>(entity);
    REQUIRE(collider.width == 32.0f);
    REQUIRE(collider.is_solid);

    auto& ai = em.registry().get<AIController>(entity);
    REQUIRE(ai.state == AIController::State::Idle); // aggro_radius > 0 → starts Idle
    REQUIRE(ai.separation_strength == Catch::Approx(0.6f));
    REQUIRE(ai.arrival_radius == Catch::Approx(128.0f));
}

TEST_CASE("ConfigLoader loads player entity with correct values", "[config]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    auto entity = ConfigLoader::loadEntity(em, "config/entities/player.json");

    REQUIRE(em.registry().valid(entity));

    auto& tag = em.registry().get<Tag>(entity);
    REQUIRE(tag.name == "player");

    auto& transform = em.registry().get<Transform>(entity);
    REQUIRE(transform.x == 640.0f);
    REQUIRE(transform.y == 360.0f);

    // Health is derived — must call applyInitialDerivations first.
    REQUIRE_FALSE(em.registry().all_of<Health>(entity));
    LevelingSystem::applyInitialDerivations(em);

    // Player: base_hp=50, end=1 → maxHP = 50 + floor(30 * ln(2)) = 70
    auto& health = em.registry().get<Health>(entity);
    REQUIRE(health.max == 70);
    REQUIRE(health.current == health.max);
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
