#include "systems/AggroSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

static void updateSprintFlag(AIController& ai, float distSq)
{
    if (ai.state == AIController::State::Chase && ai.sprint_multiplier > 0.0f &&
        ai.sprint_threshold > 0.0f)
        ai.sprint = distSq > ai.sprint_threshold * ai.sprint_threshold;
    else
        ai.sprint = false;
}

void AggroSystem::update(EntityManager& em)
{
    ZoneScopedN("AggroSystem");
    // Fetch player position once — identified by the Input component tag.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;

    for (auto e : em.registry().view<Input>())
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
            // Enter Attack formation when the player is within arrival radius AND
            // has a clear line of sight (no wall or obstacle in between).
            // Without the LOS check an enemy stops at an obstacle between them
            // and never navigates around it to actually reach the player.
            if (ai.arrival_radius > 0.0f && distSq <= ai.arrival_radius * ai.arrival_radius)
            {
                const bool los = !em.tile_map.valid() ||
                                 em.tile_map.hasLineOfSight(transform.x, transform.y, px, py);
                if (los)
                {
                    ai.state = AIController::State::Attack;
                    TracyMessageL("EnemyAttack");
                }
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
    }
}
