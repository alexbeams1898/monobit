#include "systems/MovementSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <vector>

// How much to shrink the entity's bounding box for movement projection checks.
// Applied as an inset on each side (so a 32x32 entity uses a 30x30 test box).
//
// Why this exists — "corner sticking": without an inset, an entity approaching
// a doorway at a slight angle gets caught on the corner of the adjacent wall
// tile. The entity's full-size box overlaps the tile's edge by 1-2 px on one
// axis, causing that axis's velocity to be zeroed even though the entity is
// clearly trying to slide past the corner. A 2 px inset absorbs that micro-
// overlap, allowing smooth corner slides while still blocking true head-on
// collisions (a 30 px box cannot pass through a 32 px wall).
static constexpr float MOVEMENT_INSET = 2.0f;

// Returns true if two center-based AABBs overlap.
static bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                        float bh)
{
    return std::abs(ax - bx) < (aw + bw) * 0.5f && std::abs(ay - by) < (ah + bh) * 0.5f;
}

void MovementSystem::update(EntityManager& em, double dt)
{
    const float fdt = static_cast<float>(dt);
    const FormulaConfig& f = em.formulas;

    // Pass 0: Stagger lock — zero velocity for any entity that can't move.
    // Must run before Pass 1 (player input) and after ChaseSystem/SteeringSystem
    // (which set enemy velocity earlier this frame) so both are covered.
    for (auto entity : em.registry().view<Staggered, Velocity>())
    {
        auto& vel = em.registry().get<Velocity>(entity);
        vel.dx = 0.0f;
        vel.dy = 0.0f;
    }

    // Pass 1: Input intent → Velocity.
    // Player speed is derived from DEX stat using the formula:
    //   finalSpeed = base * (1 + floor(dex_scale * log(DEX + 1)) / 100)
    // Entities without Stats fall back to f.movement.base (150 px/s default).
    //
    // Skip velocity update if the entity is Dodging (dodge impulse carries through)
    // or Staggered (guard break / parry result locks movement briefly).
    for (auto [entity, input, vel] : em.registry().view<Input, Velocity>().each())
    {
        // Dodging and Staggered states override normal movement.
        if (em.registry().all_of<Dodging>(entity) || em.registry().all_of<Staggered>(entity))
            continue;

        float speed = f.movement.base;
        if (em.registry().all_of<Stats>(entity))
        {
            const int dex = em.registry().get<Stats>(entity).dex;
            speed *= (1.0f +
                      std::floor(f.movement.dex_scale * std::log(static_cast<float>(dex) + 1.0f)) /
                          100.0f);
        }

        vel.dx = input.move_x * speed;
        vel.dy = input.move_y * speed;
    }

    // Gather static solid colliders for axis-projection checks below.
    // "Static" = has Collider + is_solid, but no Velocity component.
    // Collected once per frame so the inner loop doesn't re-query the registry.
    auto allColliders = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> statics;
    statics.reserve(64);
    for (auto e : allColliders)
    {
        if (!em.registry().all_of<Velocity>(e) && allColliders.get<Collider>(e).is_solid)
            statics.push_back(e);
    }

    // Pass 2: Velocity → Transform with static wall projection.
    //
    // For each dynamic entity, X and Y movement are tested against every
    // static solid independently:
    //   - Compute the candidate new position on that axis.
    //   - If it would overlap a static solid, zero the velocity on that axis
    //     and keep the current position — the entity slides along the wall
    //     rather than penetrating or bouncing.
    //   - The Y test uses the resolved X (after the X test) so corner slides
    //     are handled correctly.
    //
    // This projection approach prevents overlaps from ever reaching
    // CollisionSystem, so CollisionSystem can focus exclusively on
    // dynamic-vs-dynamic interactions.
    for (auto [entity, vel, transform] : em.registry().view<Velocity, Transform>().each())
    {
        const Collider* col = em.registry().try_get<Collider>(entity);
        if (!col)
        {
            // No collider — integrate unconditionally.
            transform.x += vel.dx * fdt;
            transform.y += vel.dy * fdt;
            continue;
        }

        // Inset box dimensions used for movement projection.
        // Slightly smaller than the entity's true collider — see MOVEMENT_INSET.
        const float mw = col->width - MOVEMENT_INSET;
        const float mh = col->height - MOVEMENT_INSET;

        // X-axis projection: candidate position after horizontal movement.
        float nx = transform.x + vel.dx * fdt;
        for (auto se : statics)
        {
            const auto& st = allColliders.get<Transform>(se);
            const auto& sc = allColliders.get<Collider>(se);
            if (aabbOverlap(nx, transform.y, mw, mh, st.x, st.y, sc.width, sc.height))
            {
                vel.dx = 0.0f;
                nx = transform.x; // stay in place on X
                break;
            }
        }

        // Y-axis projection: candidate position after vertical movement.
        // Uses resolved X (nx) so a corner slide doesn't re-trigger the X wall.
        float ny = transform.y + vel.dy * fdt;
        for (auto se : statics)
        {
            const auto& st = allColliders.get<Transform>(se);
            const auto& sc = allColliders.get<Collider>(se);
            if (aabbOverlap(nx, ny, mw, mh, st.x, st.y, sc.width, sc.height))
            {
                vel.dy = 0.0f;
                ny = transform.y; // stay in place on Y
                break;
            }
        }

        transform.x = nx;
        transform.y = ny;
    }

    // Pass 3: Update FacingDirection from current velocity for all entities.
    // This runs after position integration so the facing reflects where the
    // entity actually moved this frame (wall projections may zero an axis).
    for (auto [entity, vel, facing] : em.registry().view<Velocity, FacingDirection>().each())
    {
        const float len = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
        if (len > 0.0f)
        {
            facing.dx = vel.dx / len;
            facing.dy = vel.dy / len;
        }
        // If vel is zero, keep the last known facing direction.
    }
}
