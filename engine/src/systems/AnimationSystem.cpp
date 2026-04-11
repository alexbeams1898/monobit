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

static void updateStandaloneDirection(entt::registry& reg, entt::entity entity, Animation& anim)
{
    if (anim.direction_count <= 1)
        return;

    // Entities with FacingDirection (player) always face aim direction.
    // Walk animation reverse is handled by the backpedaling flag.
    // Entities without FacingDirection (AI) use movement direction when walking.
    const auto* facing = reg.try_get<FacingDirection>(entity);
    if (facing)
    {
        anim.dir = snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
    }
    else if (anim.state == AnimState::Walk || anim.state == AnimState::Run)
    {
        const auto* vel = reg.try_get<Velocity>(entity);
        if (vel)
            anim.dir = snapMovement(vel->dx, vel->dy, anim.direction_count);
    }
}

static void advanceAnimation(entt::registry& reg, entt::entity entity, Animation& anim, float dt)
{
    const auto& sd = anim.states[static_cast<int>(anim.state)];
    float frameDuration = sd.duration;

    bool reverse = false;
    if (anim.state == AnimState::Walk)
    {
        const auto* facing = reg.try_get<FacingDirection>(entity);
        if (facing)
        {
            frameDuration *= facing->walk_anim_speed;
            reverse = facing->backpedaling;
        }
    }
    else if (anim.state == AnimState::Attack)
    {
        const auto* facing = reg.try_get<FacingDirection>(entity);
        if (facing && facing->attack_anim_speed > 0.0f)
            frameDuration *= facing->attack_anim_speed;
    }

    if (frameDuration > 0.0f && sd.frames > 1)
    {
        // Death, Hit and Attack are one-shot: freeze on the last frame. Hit
        // only plays during Dead (non-lethal hits use TintSystem flash). Attack
        // is gated by AttackLocked from the game side -- letting it loop made
        // heavy weapons visibly play their swing twice when the lock window
        // exceeded the animation length.
        const bool freezeLast = (anim.state == AnimState::Death || anim.state == AnimState::Hit ||
                                 anim.state == AnimState::Attack);
        anim.frame_timer += dt;
        while (anim.frame_timer >= frameDuration)
        {
            anim.frame_timer -= frameDuration;
            if (freezeLast)
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
        updateStandaloneDirection(reg, entity, anim);

        // --- 3. Advance frame timer ---
        advanceAnimation(reg, entity, anim, dt);

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
