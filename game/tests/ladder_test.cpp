#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/LadderSystem.h"
#include "test_helpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// LadderSystem tests -- no window, no GPU required.
// ---------------------------------------------------------------------------

static entt::entity makePlayer(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<PlayerActions>(e);
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<Camera>(e, Camera{x, y, true});
    em.registry().emplace<Health>(e, Health{100, 100});
    return e;
}

static entt::entity makeLadder(EntityManager& em, float x, float y, bool spawning = false)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y, 0.0f, spawning ? 0.0f : 1.0f});
    Ladder l;
    l.radius = 48.0f;
    l.spawn_duration = 0.5f;
    l.spawning = spawning;
    l.spawn_timer = spawning ? 0.0f : l.spawn_duration;
    em.registry().emplace<Ladder>(e, l);
    return e;
}

TEST_CASE("Ladder spawn animation scales from 0 to 1", "[ladder]")
{
    EntityManager em;
    emplaceGameConfigs(em);
    makePlayer(em, 0.0f, 0.0f);
    auto ladder = makeLadder(em, 200.0f, 200.0f, true);

    // After half the spawn duration, scale should be ~0.5.
    LadderSystem::update(em, 0.25);
    const auto& t = em.registry().get<Transform>(ladder);
    REQUIRE(t.scale == Catch::Approx(0.5f));
    REQUIRE(em.registry().get<Ladder>(ladder).spawning);

    // After another 0.25s, spawn completes.
    LadderSystem::update(em, 0.25);
    const auto& t2 = em.registry().get<Transform>(ladder);
    REQUIRE(t2.scale == Catch::Approx(1.0f));
    REQUIRE_FALSE(em.registry().get<Ladder>(ladder).spawning);
}

TEST_CASE("Ladder interaction triggers next wave when in range", "[ladder]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    // Set up wave state so startNextWave can proceed.
    auto& ws = em.registry().ctx().get<WaveState>();
    ws.phase = WaveState::Phase::SafeRoom;
    ws.current_wave = 1;

    auto player = makePlayer(em, 100.0f, 100.0f);
    auto& actions = em.registry().get<PlayerActions>(player);
    actions.interact = true;

    // Ladder within radius (48 px).
    makeLadder(em, 120.0f, 100.0f, false);

    LadderSystem::update(em, 0.016);

    // Wave should have advanced.
    REQUIRE(ws.current_wave == 2);
}

TEST_CASE("Ladder interaction does nothing when out of range", "[ladder]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto& ws = em.registry().ctx().get<WaveState>();
    ws.phase = WaveState::Phase::SafeRoom;
    ws.current_wave = 1;

    auto player = makePlayer(em, 0.0f, 0.0f);
    auto& actions = em.registry().get<PlayerActions>(player);
    actions.interact = true;

    // Ladder far away (beyond 48 px radius).
    makeLadder(em, 500.0f, 500.0f, false);

    LadderSystem::update(em, 0.016);

    // Wave should NOT have advanced.
    REQUIRE(ws.current_wave == 1);
}

TEST_CASE("Ladder interaction blocked during spawn animation", "[ladder]")
{
    EntityManager em;
    emplaceGameConfigs(em);

    auto& ws = em.registry().ctx().get<WaveState>();
    ws.phase = WaveState::Phase::SafeRoom;
    ws.current_wave = 1;

    auto player = makePlayer(em, 100.0f, 100.0f);
    auto& actions = em.registry().get<PlayerActions>(player);
    actions.interact = true;

    // Ladder in range but still spawning.
    makeLadder(em, 120.0f, 100.0f, true);

    LadderSystem::update(em, 0.016);

    // Should not advance -- still spawning.
    REQUIRE(ws.current_wave == 1);
}
