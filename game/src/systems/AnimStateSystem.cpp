#include "systems/AnimStateSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <tracy/Tracy.hpp>

// Priority: Dead(Hit) > Staggered(Idle) > CriticalAttacking(Attack) > AttackLocked(Attack)
//           > moving(Run/Walk) > Idle.
// Non-lethal hits use the TintSystem white flash instead of a dedicated anim row.
// On death we play the hurt animation so the death moment has a visible reaction.
// Staggered locks to Idle so the run/walk animation doesn't keep playing while the
// entity is knocked still and movement input is being ignored.
static AnimState resolveStandaloneState(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<Dead>(entity))
        return AnimState::Hit;
    if (reg.all_of<Staggered>(entity))
        return AnimState::Idle;
    if (reg.all_of<CriticalAttacking>(entity))
        return AnimState::Attack;
    if (reg.all_of<AttackLocked>(entity))
        return AnimState::Attack;

    // Player: use raw input (MovementIntent) so wall collisions don't cause
    // animation flicker. Velocity oscillates when the exponential blend pumps
    // it back up each tick only for collision to zero it again.
    // Enemies: use post-collision velocity (no MovementIntent).
    const auto* intent = reg.try_get<MovementIntent>(entity);
    bool moving = false;
    if (intent)
    {
        moving = (intent->dx != 0.0f || intent->dy != 0.0f);
    }
    else
    {
        const auto* vel = reg.try_get<Velocity>(entity);
        const float speedSq = vel ? (vel->dx * vel->dx + vel->dy * vel->dy) : 0.0f;
        moving = (speedSq > 1.0f);
    }
    if (moving)
    {
        const auto* facing = reg.try_get<FacingDirection>(entity);
        if (facing && facing->sprinting && !facing->backpedaling)
            return AnimState::Run;
        return AnimState::Walk;
    }
    return AnimState::Idle;
}

void AnimStateSystem::update(EntityManager& em)
{
    ZoneScopedN("AnimStateSystem");
    auto& reg = em.registry();

    for (auto [entity, anim] : reg.view<Animation>().each())
    {
        AnimState resolved = resolveStandaloneState(reg, entity);
        // Fall back to Walk if the resolved state has no frames configured.
        if (anim.states[static_cast<int>(resolved)].frames <= 0)
            resolved = AnimState::Walk;
        anim.state = resolved;
    }
}
