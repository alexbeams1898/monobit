#include "systems/ChaseSystem.h"

#include "ecs/Components.h"

#include <cmath>

void ChaseSystem::update(EntityManager& em)
{
    // Fetch the player's world position once — all chasers use this same value.
    // Player is identified by the Input component (set at startup in main.cpp).
    // If no player exists yet, skip the entire update.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;

    for (auto [entity, transform] : em.registry().view<Transform, Input>().each())
    {
        px = transform.x;
        py = transform.y;
        playerFound = true;
        break;
    }

    if (!playerFound)
        return;

    // Sweep every AI entity in one tight loop — no per-entity indirection.
    // All arithmetic operates on values already in registers or L1 cache.
    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        if (ai.state != AIController::State::Chase)
            continue;

        const float dx = px - transform.x;
        const float dy = py - transform.y;
        const float len = std::sqrt(dx * dx + dy * dy);

        // Stop applying velocity when practically on top of the player — avoids
        // jitter and division-by-zero. Collision will handle the final separation.
        if (len > 0.5f)
        {
            vel.dx = (dx / len) * ai.speed;
            vel.dy = (dy / len) * ai.speed;
        }
        else
        {
            vel.dx = 0.0f;
            vel.dy = 0.0f;
        }
    }
}
