#include "systems/AnimStateSystem.h"

#include "ecs/BalanceConfig.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <tracy/Tracy.hpp>

// Priority: Dead(Hit) > Staggered(Idle) > CriticalAttacking(Attack) > AttackLocked(Attack)
//           > moving(Run/Walk) > Idle.
// Non-lethal hits use the TintSystem white flash instead of a dedicated anim row.
// On death we play the hurt animation so the death moment has a visible reaction.
// Staggered locks to Idle so the run/walk animation doesn't keep playing while the
// entity is knocked still and movement input is being ignored.
static AnimState resolveState(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<Dead>(entity))
        return AnimState::Hit;
    if (reg.all_of<Staggered>(entity))
        return AnimState::Idle;
    if (reg.all_of<CriticalAttacking>(entity))
        return AnimState::Attack;
    if (reg.all_of<AttackLocked>(entity))
        return AnimState::Attack;

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

    const auto* rowIndex = reg.ctx().find<AnimRowIndex>();

    for (auto [entity, anim, rowCfg] : reg.view<Animation, AnimRowConfig>().each())
    {
        AnimState resolved = resolveState(reg, entity);

        // Fall back to Walk if the resolved state has no frames configured.
        const auto& rd = rowCfg.rows[static_cast<int>(resolved)];
        if (rd.frames <= 0)
            resolved = AnimState::Walk;

        const auto& row = rowCfg.rows[static_cast<int>(resolved)];

        // Write playback fields for engine AnimationSystem.
        anim.current_row = row.row;
        anim.current_frames = row.frames;
        anim.current_duration = row.duration;
        anim.freeze_on_last = row.freeze_on_last;

        // Backpedaling and speed modifiers.
        anim.reverse = false;
        anim.speed_multiplier = 1.0f;
        anim.frame_mask.clear();

        if (resolved == AnimState::Walk)
        {
            const auto* facing = reg.try_get<FacingDirection>(entity);
            if (facing)
            {
                anim.speed_multiplier = facing->walk_anim_speed;
                anim.reverse = facing->backpedaling;
            }
        }
        else if (resolved == AnimState::Attack)
        {
            const auto* facing = reg.try_get<FacingDirection>(entity);
            if (facing && facing->attack_anim_speed > 0.0f)
                anim.speed_multiplier = facing->attack_anim_speed;

            // Read the weapon that initiated this attack (left or right hand).
            const auto* al = reg.try_get<AttackLocked>(entity);
            const Weapon* w = nullptr;
            if (al != nullptr && al->left_hand)
                w = reg.try_get<LeftWeapon>(entity);
            if (w == nullptr)
                w = reg.try_get<Weapon>(entity);

            if (w != nullptr && !w->attack_anim.empty() && w->attack_anim != "slash" &&
                rowIndex != nullptr)
            {
                const auto it = rowIndex->rows.find(w->attack_anim);
                if (it != rowIndex->rows.end())
                {
                    anim.current_row = it->second.row;
                    anim.current_frames = it->second.frames;
                    anim.current_duration = it->second.duration;
                }
            }

            if (w != nullptr && !w->shoot_frames.empty())
                anim.frame_mask = w->shoot_frames;
        }
    }
}
