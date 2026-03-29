#include "systems/AnimationSystem.h"

#include "ecs/Components.h"
#include "utils/DirectionUtils.h"

#include <cmath>
#include <tracy/Tracy.hpp>

// AnimationSystem -- frame advancement and sprite src rect computation.
//
// Does NOT resolve animation state. anim.state is written each game tick by
// AnimStateSystem (game/src/systems/AnimStateSystem.cpp), which reads game
// components (Dead, DamageFeedback, AttackLocked, etc.) that the engine
// has no knowledge of.
//
// This system detects state changes via anim.prev_state, resets the frame
// counter on change, then advances the frame timer.

using engine::direction::dirToColumnIndex;
using engine::direction::snapFacing;
using engine::direction::snapMovement;

// ---------------------------------------------------------------------------
// Per-entity direction update
// ---------------------------------------------------------------------------

static void updateBodyPartDirection(entt::registry& reg, const BodyPart& bp, Animation& anim)
{
    if (bp.direction_from_facing)
    {
        const auto* facing = reg.try_get<FacingDirection>(bp.parent);
        if (facing)
            anim.dir =
                snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
    }
    else
    {
        // When backpedaling, lower body faces aim direction (same as upper body)
        // so the character visually faces the enemy while stepping backward.
        const auto* facing = reg.try_get<FacingDirection>(bp.parent);
        if (facing && facing->backpedaling)
        {
            anim.dir =
                snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
            return;
        }

        // MovementIntent = authoritative direction (raw input before collision).
        // Velocity = fallback for entities without intent (AI).
        // When intent exists but is zero, the player stopped -- don't let
        // decaying blend velocity flip the animation direction.
        const auto* intent = reg.try_get<MovementIntent>(bp.parent);
        if (intent)
        {
            if (intent->dx != 0.0f || intent->dy != 0.0f)
            {
                anim.dir = snapMovement(intent->dx, intent->dy, anim.direction_count);
                return;
            }
        }
        else
        {
            const auto* vel = reg.try_get<Velocity>(bp.parent);
            if (vel && (vel->dx * vel->dx + vel->dy * vel->dy) > 1.0f)
            {
                anim.dir = snapMovement(vel->dx, vel->dy, anim.direction_count);
                return;
            }
        }

        // Idle: full body turns to face aim direction
        if (facing)
            anim.dir =
                snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
    }
}

static void updateStandaloneDirection(entt::registry& reg, entt::entity entity, Animation& anim)
{
    if (anim.direction_count <= 1)
        return;

    if (anim.state == AnimState::Walk)
    {
        // MovementIntent = authoritative; Velocity = fallback for entities without intent.
        const auto* intent = reg.try_get<MovementIntent>(entity);
        if (intent)
        {
            if (intent->dx != 0.0f || intent->dy != 0.0f)
                anim.dir = snapMovement(intent->dx, intent->dy, anim.direction_count);
        }
        else
        {
            const auto* vel = reg.try_get<Velocity>(entity);
            if (vel)
                anim.dir = snapMovement(vel->dx, vel->dy, anim.direction_count);
        }
    }
    else if (const auto* facing = reg.try_get<FacingDirection>(entity))
    {
        anim.dir = snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
    }
}

static void advanceAnimation(entt::registry& reg, entt::entity entity, const BodyPart* bp,
                             Animation& anim, float dt)
{
    const auto& sd = anim.states[static_cast<int>(anim.state)];
    float frameDuration = sd.duration;

    bool reverse = false;
    if (anim.state == AnimState::Walk)
    {
        const entt::entity walkEntity = (bp && reg.valid(bp->parent)) ? bp->parent : entity;
        const auto* facing = reg.try_get<FacingDirection>(walkEntity);
        if (facing)
        {
            frameDuration *= facing->walk_anim_speed;
            reverse = facing->backpedaling;
        }
    }

    if (frameDuration > 0.0f && sd.frames > 1)
    {
        anim.frame_timer += dt;
        while (anim.frame_timer >= frameDuration)
        {
            anim.frame_timer -= frameDuration;
            if (anim.state == AnimState::Death)
            {
                if (anim.frame_index < sd.frames - 1)
                    anim.frame_index++;
            }
            else if (reverse)
            {
                anim.frame_index = (anim.frame_index - 1 + sd.frames) % sd.frames;
            }
            else
            {
                anim.frame_index = (anim.frame_index + 1) % sd.frames;
            }
        }
    }
}

void AnimationSystem::update(EntityManager& em, float dt)
{
    ZoneScopedN("AnimationSystem");
    auto& reg = em.registry();

    for (auto [entity, anim, sprite] : reg.view<Animation, Sprite>().each())
    {
        // --- 1. Detect state change (set by AnimStateSystem each game tick) ---
        if (anim.state != anim.prev_state)
        {
            anim.frame_index = 0;
            anim.frame_timer = 0.0f;
            anim.prev_state = anim.state;
        }

        // --- 2. Direction ---
        const BodyPart* bp = reg.try_get<BodyPart>(entity);
        if (bp && reg.valid(bp->parent))
            updateBodyPartDirection(reg, *bp, anim);
        else
            updateStandaloneDirection(reg, entity, anim);

        // --- 3. Advance frame timer ---
        advanceAnimation(reg, entity, bp, anim, dt);

        // --- 4. Compute sprite src rect ---
        const auto& sd = anim.states[static_cast<int>(anim.state)];
        const auto mapping =
            dirToColumnIndex(anim.dir, anim.direction_count, anim.unique_diagonals);
        const int dirOffset = mapping.column * anim.max_frames_per_state;
        const int col = dirOffset + anim.frame_index;

        sprite.src_x = col * anim.frame_width;
        sprite.src_y = sd.row * anim.frame_height;
        sprite.src_w = anim.frame_width;
        sprite.src_h = anim.frame_height;
        sprite.flip_x = mapping.flip;
    }
}
