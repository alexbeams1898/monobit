#include "systems/AnimationSystem.h"

#include "ecs/Components.h"

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

// Snap a velocity vector to the nearest cardinal direction.
// No hysteresis -- WASD input is digital; ties (exact diagonals) prefer vertical.
static CardinalDir snapToCardinal(float dx, float dy)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    if (ay >= ax)
        return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
    return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
}

// Snap a facing vector to the nearest cardinal direction with hysteresis.
// Once a direction is set, require the off-axis component to exceed the
// on-axis by at least HYSTERESIS_RATIO before switching. Prevents jitter
// at axis boundaries (e.g. mouse near 45 degrees from player).
static constexpr float HYSTERESIS_RATIO = 0.15f;

static CardinalDir snapWithHysteresis(float dx, float dy, CardinalDir current)
{
    const float ax = std::abs(dx);
    const float ay = std::abs(dy);
    const bool currentIsHorizontal = (current == CardinalDir::East || current == CardinalDir::West);

    if (currentIsHorizontal)
    {
        if (ay > ax + ax * HYSTERESIS_RATIO)
            return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    }
    if (ax > ay + ay * HYSTERESIS_RATIO)
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
}

static void updateBodyPartDirection(entt::registry& reg, const BodyPart& bp, Animation& anim)
{
    if (bp.direction_from_facing)
    {
        const auto* facing = reg.try_get<FacingDirection>(bp.parent);
        if (facing)
            anim.dir = snapWithHysteresis(facing->render_dx, facing->render_dy, anim.dir);
    }
    else
    {
        // Prefer MovementIntent (raw directional input before collision) over post-collision
        // Velocity so wall contact doesn't flip the walk-direction animation.
        const auto* intent = reg.try_get<MovementIntent>(bp.parent);
        if (intent && (intent->dx != 0.0f || intent->dy != 0.0f))
        {
            anim.dir = snapToCardinal(intent->dx, intent->dy);
            return;
        }
        const auto* vel = reg.try_get<Velocity>(bp.parent);
        if (vel && (vel->dx * vel->dx + vel->dy * vel->dy) > 1.0f)
            anim.dir = snapToCardinal(vel->dx, vel->dy);
    }
}

static void updateStandaloneDirection(entt::registry& reg, entt::entity entity, Animation& anim)
{
    if (anim.state == AnimState::Walk)
    {
        // Prefer MovementIntent over post-collision Velocity for direction snapping.
        const auto* intent = reg.try_get<MovementIntent>(entity);
        if (intent && (intent->dx != 0.0f || intent->dy != 0.0f))
        {
            anim.dir = snapToCardinal(intent->dx, intent->dy);
            return;
        }
        const auto* vel = reg.try_get<Velocity>(entity);
        if (vel)
            anim.dir = snapToCardinal(vel->dx, vel->dy);
    }
    else if (const auto* facing = reg.try_get<FacingDirection>(entity))
    {
        anim.dir = snapWithHysteresis(facing->render_dx, facing->render_dy, anim.dir);
    }
}

static void advanceAnimation(entt::registry& reg, entt::entity entity, const BodyPart* bp,
                             Animation& anim, float dt)
{
    const auto& sd = anim.states[static_cast<int>(anim.state)];
    float frameDuration = sd.duration;

    if (anim.state == AnimState::Walk)
    {
        const entt::entity sprintEntity = (bp && reg.valid(bp->parent)) ? bp->parent : entity;
        const auto* facing = reg.try_get<FacingDirection>(sprintEntity);
        const bool sprinting = facing && facing->sprinting;
        static constexpr float SPRINT_ANIM_SPEED = 0.65f;
        if (sprinting)
            frameDuration *= SPRINT_ANIM_SPEED;
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

        // --- 2. Cardinal direction ---
        const BodyPart* bp = reg.try_get<BodyPart>(entity);
        if (bp && reg.valid(bp->parent))
            updateBodyPartDirection(reg, *bp, anim);
        else
            updateStandaloneDirection(reg, entity, anim);

        // --- 3. Advance frame timer ---
        advanceAnimation(reg, entity, bp, anim, dt);

        // --- 4. Compute sprite src rect ---
        const auto& sd = anim.states[static_cast<int>(anim.state)];
        const int dirOffset = static_cast<int>(anim.dir) * anim.max_frames_per_state;
        const int col = dirOffset + anim.frame_index;

        sprite.src_x = col * anim.frame_width;
        sprite.src_y = sd.row * anim.frame_height;
        sprite.src_w = anim.frame_width;
        sprite.src_h = anim.frame_height;
    }
}
