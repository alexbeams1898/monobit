#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "systems/AggroSystem.h"

#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// AggroSystem tests
//
// No SDL/GL context required — the system only reads Transform and
// AIController components and mutates AIController::state.
// ---------------------------------------------------------------------------

static entt::entity makePlayer(EntityManager& em, float x, float y)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    em.registry().emplace<PlayerActions>(e);
    return e;
}

static entt::entity makeEnemy(EntityManager& em, float x, float y, float aggro_radius)
{
    auto e = em.create();
    em.registry().emplace<Transform>(e, Transform{x, y});
    AIController ai;
    ai.state = AIController::State::Idle;
    ai.aggro_radius = aggro_radius;
    em.registry().emplace<AIController>(e, ai);
    return e;
}

TEST_CASE("AggroSystem: enemy outside radius stays Idle", "[aggro]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 400.0f, 0.0f, 300.0f); // 400 px away, radius 300

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Idle);
}

TEST_CASE("AggroSystem: enemy inside radius transitions to Chase", "[aggro]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 200.0f, 0.0f, 300.0f); // 200 px away, radius 300

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}

TEST_CASE("AggroSystem: enemy exactly on radius boundary transitions to Chase", "[aggro]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 300.0f, 0.0f, 300.0f); // exactly on boundary

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}

TEST_CASE("AggroSystem: enemy with zero radius is ignored", "[aggro]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 0.0f); // right on player, but no radius

    AggroSystem::update(em);

    // Zero radius = not managed by AggroSystem — state unchanged
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Idle);
}

TEST_CASE("AggroSystem: already-Chase enemy is unaffected", "[aggro]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 400.0f, 0.0f, 300.0f); // outside radius
    em.registry().patch<AIController>(enemy, [](AIController& ai)
                                      { ai.state = AIController::State::Chase; });

    AggroSystem::update(em);

    // Already Chase — not reverted to Idle
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}

TEST_CASE("AggroSystem: no player — no transition", "[aggro]")
{
    EntityManager em;
    // No player entity (no PlayerActions component)
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 300.0f);

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Idle);
}

// ---------------------------------------------------------------------------
// Chase → Attack and Attack → Chase transition tests
// ---------------------------------------------------------------------------

TEST_CASE("AggroSystem: Chase enemy enters Attack when inside arrival_radius", "[aggro][attack]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 100.0f, 0.0f, 300.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Chase;
                                          ai.arrival_radius = 200.0f;
                                          ai.attack_radius = 48.0f;
                                      });

    AggroSystem::update(em);

    // dist=100 ≤ arrival_radius=200 and attack_radius > 0 → Attack
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Attack);
}

TEST_CASE("AggroSystem: Chase enemy stays Chase when outside arrival_radius", "[aggro][attack]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 300.0f, 0.0f, 400.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Chase;
                                          ai.arrival_radius = 200.0f;
                                          ai.attack_radius = 48.0f;
                                      });

    AggroSystem::update(em);

    // dist=300 > arrival_radius=200 → stays Chase
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}

TEST_CASE("AggroSystem: Attack enemy stays Attack when player is far but not sprinting",
          "[aggro][attack]")
{
    // Slot positions track the player, so Attack-state enemies follow naturally.
    // No distance-based breakoff -- deaggro_radius catches the "player left" case.
    EntityManager em;
    makePlayer(em, 400.0f, 0.0f); // far -- beyond arrival_radius
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 300.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Attack;
                                          ai.arrival_radius = 128.0f;
                                          ai.attack_radius = 48.0f;
                                      });

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Attack);
}

TEST_CASE("AggroSystem: Attack enemy returns to Chase when player sprints", "[aggro][attack]")
{
    EntityManager em;
    auto player = makePlayer(em, 400.0f, 0.0f);
    em.registry().get<PlayerActions>(player).sprint = true;
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 300.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Attack;
                                          ai.arrival_radius = 128.0f;
                                          ai.attack_radius = 48.0f;
                                      });

    AggroSystem::update(em);

    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}

TEST_CASE("AggroSystem: Attack enemy holds Attack when player is still close", "[aggro][attack]")
{
    EntityManager em;
    makePlayer(em, 100.0f, 0.0f); // within arrival_radius
    auto enemy = makeEnemy(em, 0.0f, 0.0f, 300.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Attack;
                                          ai.arrival_radius = 128.0f;
                                          ai.attack_radius = 48.0f;
                                      });

    AggroSystem::update(em);

    // dist=100 ≤ arrival_radius*1.2 = 153.6 → stays Attack
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Attack);
}

TEST_CASE("AggroSystem: Chase enemy with no attack_radius never transitions to Attack",
          "[aggro][attack]")
{
    EntityManager em;
    makePlayer(em, 0.0f, 0.0f);
    auto enemy = makeEnemy(em, 50.0f, 0.0f, 300.0f);
    em.registry().patch<AIController>(enemy,
                                      [](AIController& ai)
                                      {
                                          ai.state = AIController::State::Chase;
                                          ai.arrival_radius = 200.0f;
                                          ai.attack_radius = 0.0f; // disabled
                                      });

    AggroSystem::update(em);

    // attack_radius=0 → Chase→Attack never fires
    REQUIRE(em.registry().get<AIController>(enemy).state == AIController::State::Chase);
}
