#include "systems/AggroSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <tracy/Tracy.hpp>

static void tracyEntityMsg(const char* event, entt::entity entity, float dist = -1.0f)
{
    static char buf[64]; // NOLINT(concurrency-mt-unsafe) -- single-threaded game loop
    if (dist >= 0.0f)
        std::snprintf(buf, sizeof(buf), "%s e%u d=%.0f", event,
                      static_cast<unsigned>(entt::to_integral(entity)), dist);
    else
        std::snprintf(buf, sizeof(buf), "%s e%u", event,
                      static_cast<unsigned>(entt::to_integral(entity)));
    TracyMessage(buf, std::strlen(buf));
}

static void updateSprintFlag(AIController& ai, float distSq, const Stamina* sta)
{
    if (ai.state == AIController::State::Chase && ai.sprint_multiplier > 0.0f &&
        ai.sprint_threshold > 0.0f)
    {
        // Don't sprint inside attack range — save stamina for swings.
        const float minDist =
            (ai.attack_radius > ai.sprint_threshold) ? ai.attack_radius : ai.sprint_threshold;
        const bool inRange = distSq > minDist * minDist;

        if (sta && sta->max_stamina > 0.0f)
        {
            const float ratio = sta->current / sta->max_stamina;
            // Hysteresis: need 50% to start, stop at 20%.
            // Prevents exhaustion stagger → micro-sprint death spiral.
            if (ai.sprint)
                ai.sprint = inRange && ratio > 0.2f;
            else
                ai.sprint = inRange && ratio > 0.5f;
        }
        else
        {
            ai.sprint = inRange;
        }
    }
    else
    {
        ai.sprint = false;
    }
}

// Find the player position + sprint state. Returns false if there is no
// player entity with a Transform this frame.
static bool findPlayer(EntityManager& em, float& px, float& py, bool& sprinting)
{
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            sprinting = em.registry().get<PlayerActions>(e).sprint;
            return true;
        }
        break;
    }
    return false;
}

// Snap facing toward the player so newly-aggroed enemies don't run backward.
static void snapFacingToPlayer(EntityManager& em, entt::entity entity, float dx, float dy,
                               float distSq)
{
    auto* facing = em.registry().try_get<FacingDirection>(entity);
    if (facing == nullptr)
        return;
    const float dist = std::sqrt(distSq);
    if (dist <= 0.0f)
        return;
    facing->dx = dx / dist;
    facing->dy = dy / dist;
    facing->render_dx = facing->dx;
    facing->render_dy = facing->dy;
    facing->aim_dx = facing->dx;
    facing->aim_dy = facing->dy;
}

// Idle -> Chase transition. True if the state flipped.
static bool tryAggro(EntityManager& em, entt::entity entity, AIController& ai, float dx, float dy,
                     float distSq, float px, float py, float tx, float ty)
{
    if (ai.aggro_radius <= 0.0f || distSq > ai.aggro_radius * ai.aggro_radius)
        return false;
    if (em.tile_map.valid() && !em.tile_map.hasLineOfSight(tx, ty, px, py))
        return false;
    ai.state = AIController::State::Chase;
    snapFacingToPlayer(em, entity, dx, dy, distSq);
    tracyEntityMsg("EnemyAggro", entity, std::sqrt(distSq));
    return true;
}

// Chase state transitions: deaggro (too far) or advance to Attack.
static void updateChaseState(EntityManager& em, entt::entity entity, AIController& ai, float distSq,
                             float px, float py, float tx, float ty, bool playerSprinting)
{
    if (ai.deaggro_radius > 0.0f && distSq > ai.deaggro_radius * ai.deaggro_radius)
    {
        ai.state = AIController::State::Idle;
        tracyEntityMsg("EnemyDeaggro", entity, std::sqrt(distSq));
        return;
    }

    // Enter Attack when within arrival radius AND line of sight is clear.
    // Suppress while the player is kiting — enemies stay in Chase (with sprint)
    // and follow via flow field. Battle circle forms when the player stops.
    const bool inAttackRange = !playerSprinting && ai.attack_radius > 0.0f &&
                               ai.arrival_radius > 0.0f &&
                               distSq <= ai.arrival_radius * ai.arrival_radius;
    if (!inAttackRange)
        return;
    if (em.tile_map.valid() && !em.tile_map.hasLineOfSight(tx, ty, px, py))
        return;
    ai.state = AIController::State::Attack;
    tracyEntityMsg("EnemyAttack", entity, std::sqrt(distSq));
}

// Attack state: break back to Chase when the player sprints away.
static void updateAttackState(entt::entity entity, AIController& ai, float distSq,
                              bool playerSprinting)
{
    if (!playerSprinting)
        return;
    ai.state = AIController::State::Chase;
    ai.slot_angle = AIController::NO_SLOT;
    tracyEntityMsg("SprintBreakOff", entity, std::sqrt(distSq));
}

void AggroSystem::update(EntityManager& em)
{
    ZoneScopedN("AggroSystem");
    float px = 0.0f;
    float py = 0.0f;
    bool playerSprinting = false;
    if (!findPlayer(em, px, py, playerSprinting))
        return;

    // Sweep all AI entities and manage state transitions.
    // Squared distances used throughout — avoids a sqrt per entity on the hot path.
    //
    // Idle  → Chase  : player enters aggro_radius.
    // Chase → Attack : player enters arrival_radius (entity begins surrounding).
    // Attack → Chase : player sprints away (slot positions track otherwise).
    for (auto [entity, ai, transform] : em.registry().view<AIController, Transform>().each())
    {
        const float dx = px - transform.x;
        const float dy = py - transform.y;
        const float distSq = dx * dx + dy * dy;

        if (ai.state == AIController::State::Idle)
            tryAggro(em, entity, ai, dx, dy, distSq, px, py, transform.x, transform.y);
        else if (ai.state == AIController::State::Chase)
            updateChaseState(em, entity, ai, distSq, px, py, transform.x, transform.y,
                             playerSprinting);
        else if (ai.state == AIController::State::Attack)
            updateAttackState(entity, ai, distSq, playerSprinting);

        const Stamina* sta = em.registry().try_get<Stamina>(entity);
        updateSprintFlag(ai, distSq, sta);

        if (em.registry().all_of<FacingDirection>(entity))
            em.registry().get<FacingDirection>(entity).sprinting = ai.sprint;
    }
}
