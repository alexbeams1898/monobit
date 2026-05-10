#include "systems/AnimationSystem.h"

#include "ecs/Components.h"
#include "utils/DirectionUtils.h"

#include <tracy/Tracy.hpp>

#include <cmath>

// AnimationSystem -- frame advancement and sprite src rect computation.
// Detects row changes via prev_row, resets the frame counter, then advances
// the frame timer.

using engine::direction::dirToColumnIndex;
using engine::direction::snapFacing;
using engine::direction::snapMovement;

// ---------------------------------------------------------------------------
// Per-entity direction update
// ---------------------------------------------------------------------------

static void updateDirection(entt::registry& reg, entt::entity entity, Animation& anim)
{
    if (anim.direction_count <= 1)
        return;

    // Entities with FacingDirection always face aim direction.
    // Entities without it use movement velocity when moving.
    const auto* facing = reg.try_get<FacingDirection>(entity);
    if (facing)
    {
        anim.dir = snapFacing(facing->render_dx, facing->render_dy, anim.dir, anim.direction_count);
    }
    else if (anim.current_frames > 1 && anim.current_duration > 0.0f)
    {
        const auto* vel = reg.try_get<Velocity>(entity);
        if (vel)
            anim.dir = snapMovement(vel->dx, vel->dy, anim.direction_count);
    }
}

// ---------------------------------------------------------------------------
// Frame advancement
// ---------------------------------------------------------------------------

static void advanceAnimation(Animation& anim, float dt)
{
    const int playbackLen =
        anim.frame_mask.empty() ? anim.current_frames : static_cast<int>(anim.frame_mask.size());

    const float frameDuration = anim.current_duration * anim.speed_multiplier;
    if (frameDuration <= 0.0f || playbackLen <= 1)
        return;

    if (anim.frame_index >= playbackLen)
        anim.frame_index = playbackLen - 1;

    anim.frame_timer += dt;
    while (anim.frame_timer >= frameDuration)
    {
        anim.frame_timer -= frameDuration;
        if (anim.freeze_on_last)
        {
            if (anim.frame_index < playbackLen - 1)
                anim.frame_index++;
        }
        else if (anim.reverse)
        {
            anim.frame_index = (anim.frame_index - 1 + playbackLen) % playbackLen;
        }
        else
        {
            anim.frame_index = (anim.frame_index + 1) % playbackLen;
        }
    }
}

// ---------------------------------------------------------------------------
// Main update
// ---------------------------------------------------------------------------

void AnimationSystem::update(EntityManager& em, float dt)
{
    ZoneScopedN("AnimationSystem");
    auto& reg = em.registry();

    for (auto [entity, anim, sprite] : reg.view<Animation, Sprite>().each())
    {
        // --- 1. Detect row change ---
        if (anim.current_row != anim.prev_row)
        {
            anim.frame_index = 0;
            anim.frame_timer = 0.0f;
            anim.prev_row = anim.current_row;
        }

        // --- 2. Direction ---
        updateDirection(reg, entity, anim);

        // --- 3. Advance frame timer ---
        advanceAnimation(anim, dt);

        // --- 4. Compute sprite src rect ---
        const auto mapping = dirToColumnIndex(anim.dir, anim.direction_count);
        const int dirOffset = mapping.column * anim.max_frames_per_state;
        const int visibleFrame =
            anim.frame_mask.empty() ? anim.frame_index : anim.frame_mask[anim.frame_index];
        const int col = dirOffset + visibleFrame;

        const int newSrcX = col * anim.frame_width;
        const int newSrcY = anim.current_row * anim.frame_height;

        sprite.src_x = newSrcX;
        sprite.src_y = newSrcY;
        sprite.src_w = anim.frame_width;
        sprite.src_h = anim.frame_height;
        sprite.flip_x = mapping.flip;
    }
}
