#include "systems/AnimStateSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <tracy/Tracy.hpp>

// Priority: Dead > CriticalTarget(Hit) > CriticalAttacking(Attack)
//         > DamageFeedback(Hit) > AttackLocked(Attack) > moving(Walk) > Idle.
static AnimState resolveStandaloneState(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<Dead>(entity))
        return AnimState::Death;
    if (reg.all_of<CriticalTarget>(entity))
        return AnimState::Hit;
    if (reg.all_of<CriticalAttacking>(entity))
        return AnimState::Attack;
    if (reg.all_of<DamageFeedback>(entity))
        return AnimState::Hit;
    if (reg.all_of<AttackLocked>(entity))
        return AnimState::Attack;

    const auto* vel = reg.try_get<Velocity>(entity);
    const float speedSq = vel ? (vel->dx * vel->dx + vel->dy * vel->dy) : 0.0f;
    if (speedSq > 1.0f)
        return AnimState::Walk;
    return AnimState::Idle;
}

// Upper body (direction_from_facing=true):  Dead > CritTarget(Hit) > CritAtk(Attack)
//   > Hit > Attack > Idle.
// Lower body (direction_from_facing=false): same but Walk before Idle.
static AnimState resolveBodyPartState(entt::registry& reg, entt::entity parent,
                                      bool direction_from_facing)
{
    if (reg.all_of<Dead>(parent))
        return AnimState::Death;
    if (reg.all_of<CriticalTarget>(parent))
        return AnimState::Hit;
    if (reg.all_of<CriticalAttacking>(parent))
        return AnimState::Attack;
    if (reg.all_of<DamageFeedback>(parent))
        return AnimState::Hit;

    if (direction_from_facing)
    {
        if (reg.all_of<AttackLocked>(parent))
            return AnimState::Attack;
    }
    else
    {
        if (reg.all_of<AttackLocked>(parent))
            return AnimState::Attack;
        const auto* vel = reg.try_get<Velocity>(parent);
        const float speedSq = vel ? (vel->dx * vel->dx + vel->dy * vel->dy) : 0.0f;
        if (speedSq > 1.0f)
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
        const BodyPart* bp = reg.try_get<BodyPart>(entity);
        anim.state = (bp && reg.valid(bp->parent))
                         ? resolveBodyPartState(reg, bp->parent, bp->direction_from_facing)
                         : resolveStandaloneState(reg, entity);
    }
}
