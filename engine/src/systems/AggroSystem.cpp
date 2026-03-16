#include "systems/AggroSystem.h"

#include "ecs/Components.h"

void AggroSystem::update(EntityManager& em)
{
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
    // Idle  → Chase  : player enters aggroRadius.
    // Chase → Attack : player enters arrivalRadius (entity begins surrounding).
    // Attack → Chase : player exits arrivalRadius × 1.2 (hysteresis prevents flicker).
    for (auto [entity, ai, transform] : em.registry().view<AIController, Transform>().each())
    {
        const float dx = px - transform.x;
        const float dy = py - transform.y;
        const float distSq = dx * dx + dy * dy;

        if (ai.state == AIController::State::Idle)
        {
            if (ai.aggroRadius > 0.0f && distSq <= ai.aggroRadius * ai.aggroRadius)
                ai.state = AIController::State::Chase;
        }
        else if (ai.state == AIController::State::Chase && ai.attackRadius > 0.0f)
        {
            // Enter Attack formation when the player steps inside the arrival
            // softening zone.  Using arrivalRadius as the trigger means the
            // entity is already decelerating when it switches to ring-targeting.
            if (ai.arrivalRadius > 0.0f && distSq <= ai.arrivalRadius * ai.arrivalRadius)
                ai.state = AIController::State::Attack;
        }
        else if (ai.state == AIController::State::Attack)
        {
            // Break off and re-chase if the player runs far enough away.
            // The 1.2× hysteresis prevents rapid Chase ↔ Attack oscillation at
            // the boundary.
            const float breakRadius =
                ai.arrivalRadius > 0.0f ? ai.arrivalRadius : ai.attackRadius * 2.0f;
            const float hysteresis = breakRadius * 1.2f;
            if (distSq > hysteresis * hysteresis)
                ai.state = AIController::State::Chase;
        }
    }
}
