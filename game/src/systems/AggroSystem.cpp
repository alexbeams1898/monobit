#include "systems/AggroSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <tracy/Tracy.hpp>

static void updateSprintFlag(AIController& ai, float distSq)
{
    if (ai.state == AIController::State::Chase && ai.sprint_multiplier > 0.0f &&
        ai.sprint_threshold > 0.0f)
    {
        // Don't sprint inside attack range — save stamina for swings.
        const float minDist =
            (ai.attack_radius > ai.sprint_threshold) ? ai.attack_radius : ai.sprint_threshold;
        ai.sprint = distSq > minDist * minDist;
    }
    else
    {
        ai.sprint = false;
    }
}

void AggroSystem::update(EntityManager& em)
{
    ZoneScopedN("AggroSystem");
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;

    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            playerFound = true;
        }
        break;
    }

    if (!playerFound)
        return;

    // Sweep all AI entities and manage state transitions.
    // Squared distances used throughout — avoids a sqrt per entity on the hot path.
    //
    // Idle  → Chase  : player enters aggro_radius.
    // Chase → Attack : player enters arrival_radius (entity begins surrounding).
    // Attack → Chase : player exits arrival_radius × 1.2 (hysteresis prevents flicker).
    for (auto [entity, ai, transform] : em.registry().view<AIController, Transform>().each())
    {
        const float dx = px - transform.x;
        const float dy = py - transform.y;
        const float distSq = dx * dx + dy * dy;

        if (ai.state == AIController::State::Idle)
        {
            if (ai.aggro_radius > 0.0f && distSq <= ai.aggro_radius * ai.aggro_radius)
            {
                ai.state = AIController::State::Chase;
                TracyMessageL("EnemyAggro");
            }
        }
        else if (ai.state == AIController::State::Chase && ai.attack_radius > 0.0f)
        {
            // Enter Attack when within arrival radius AND line of sight is clear.
            if (ai.arrival_radius > 0.0f && distSq <= ai.arrival_radius * ai.arrival_radius &&
                (!em.tile_map.valid() ||
                 em.tile_map.hasLineOfSight(transform.x, transform.y, px, py)))
            {
                ai.state = AIController::State::Attack;
                TracyMessageL("EnemyAttack");
            }
        }
        else if (ai.state == AIController::State::Attack)
        {
            // Break off and re-chase if the player runs far enough away.
            // The 1.2× hysteresis prevents rapid Chase ↔ Attack oscillation at
            // the boundary.
            const float breakRadius =
                ai.arrival_radius > 0.0f ? ai.arrival_radius : ai.attack_radius * 2.0f;
            const float hysteresis = breakRadius * 1.2f;
            if (distSq > hysteresis * hysteresis)
                ai.state = AIController::State::Chase;
        }

        updateSprintFlag(ai, distSq);

        if (em.registry().all_of<FacingDirection>(entity))
            em.registry().get<FacingDirection>(entity).sprinting = ai.sprint;
    }
}
