#include "systems/AnimationSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <tracy/Tracy.hpp>

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
        // Stay horizontal unless vertical clearly dominates.
        if (ay > ax + ax * HYSTERESIS_RATIO)
            return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    }

    // Currently vertical — stay unless horizontal clearly dominates.
    if (ax > ay + ay * HYSTERESIS_RATIO)
        return dx > 0.0f ? CardinalDir::East : CardinalDir::West;
    return dy > 0.0f ? CardinalDir::South : CardinalDir::North;
}

// Resolve animation state for a standalone entity (enemies, campfire, etc.).
// Priority: Dead > Hit > Attack > Walk > Idle.
static AnimState resolveStandaloneState(entt::registry& reg, entt::entity entity)
{
    if (reg.all_of<Dead>(entity))
        return AnimState::Death;
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

// Resolve animation state for a body-part child entity.
// State components are read from the PARENT entity.
// Lower body (faces_aim=false): Dead > Hit > Walk > Idle  (no Attack)
// Upper body (faces_aim=true):  Dead > Hit > Attack > Idle (no Walk)
static AnimState resolveBodyPartState(entt::registry& reg, entt::entity parent, bool faces_aim)
{
    if (reg.all_of<Dead>(parent))
        return AnimState::Death;
    if (reg.all_of<DamageFeedback>(parent))
        return AnimState::Hit;

    if (faces_aim)
    {
        // Upper body: attack when parent is attacking, idle otherwise.
        if (reg.all_of<AttackLocked>(parent))
            return AnimState::Attack;
    }
    else
    {
        // Lower body: walk when parent is moving, idle otherwise.
        const auto* vel = reg.try_get<Velocity>(parent);
        const float speedSq = vel ? (vel->dx * vel->dx + vel->dy * vel->dy) : 0.0f;
        if (speedSq > 1.0f)
            return AnimState::Walk;
    }
    return AnimState::Idle;
}

void AnimationSystem::update(EntityManager& em, float dt)
{
    ZoneScopedN("AnimationSystem");
    auto& reg = em.registry();

    for (auto [entity, anim, sprite] : reg.view<Animation, Sprite>().each())
    {
        // --- 1. Resolve animation state ---

        const BodyPart* bp = reg.try_get<BodyPart>(entity);
        const AnimState newState = (bp && reg.valid(bp->parent))
                                       ? resolveBodyPartState(reg, bp->parent, bp->faces_aim)
                                       : resolveStandaloneState(reg, entity);

        // --- 2. Reset frame on state change ---

        if (newState != anim.state)
        {
            anim.state = newState;
            anim.frame_index = 0;
            anim.frame_timer = 0.0f;
        }

        // --- 3. Cardinal direction with hysteresis ---
        // Body parts read direction from the parent entity.
        // Lower body (faces_aim=false): direction from parent's Velocity.
        // Upper body (faces_aim=true):  direction from parent's FacingDirection.
        // Standalone entities: Walk uses own Velocity, other states use own FacingDirection.

        if (bp && reg.valid(bp->parent))
        {
            if (bp->faces_aim)
            {
                const auto* facing = reg.try_get<FacingDirection>(bp->parent);
                if (facing)
                    anim.dir = snapWithHysteresis(facing->render_dx, facing->render_dy, anim.dir);
            }
            else
            {
                const auto* vel = reg.try_get<Velocity>(bp->parent);
                if (vel && (vel->dx * vel->dx + vel->dy * vel->dy) > 1.0f)
                    anim.dir = snapWithHysteresis(vel->dx, vel->dy, anim.dir);
            }
        }
        else if (anim.state == AnimState::Walk)
        {
            const auto* vel = reg.try_get<Velocity>(entity);
            if (vel)
                anim.dir = snapWithHysteresis(vel->dx, vel->dy, anim.dir);
        }
        else if (const auto* facing = reg.try_get<FacingDirection>(entity))
        {
            anim.dir = snapWithHysteresis(facing->render_dx, facing->render_dy, anim.dir);
        }

        // --- 4. Advance frame timer ---
        // Sprint speeds up walk cycle for lower body only.

        const auto& sd = anim.states[static_cast<int>(anim.state)];
        float frameDuration = sd.duration;
        if (anim.state == AnimState::Walk)
        {
            // For body parts, check parent's Input/AIController for sprint.
            entt::entity sprintEntity = (bp && reg.valid(bp->parent)) ? bp->parent : entity;
            const auto* input = reg.try_get<Input>(sprintEntity);
            const auto* ai = reg.try_get<AIController>(sprintEntity);
            const bool sprinting = (input && input->sprint) || (ai && ai->sprint);
            if (sprinting)
                frameDuration *= 0.65f;
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

        // --- 5. Compute sprite src rect ---

        const int dirOffset = static_cast<int>(anim.dir) * anim.max_frames_per_state;
        const int col = dirOffset + anim.frame_index;
        const int row = sd.row;

        sprite.src_x = col * anim.frame_width;
        sprite.src_y = row * anim.frame_height;
        sprite.src_w = anim.frame_width;
        sprite.src_h = anim.frame_height;
    }
}
