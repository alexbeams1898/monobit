#include "systems/MovementSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <vector>

// Pixels per second at full input deflection.
// Will eventually come from a component or entity config value.
static constexpr float PLAYER_SPEED = 200.0f;

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

    // Pass 1: Input intent → Velocity.
    // Only entities with both Input and Velocity are affected here.
    for (auto [entity, input, vel] : em.registry().view<Input, Velocity>().each())
    {
        vel.dx = input.moveX * PLAYER_SPEED;
        vel.dy = input.moveY * PLAYER_SPEED;
    }

    // Gather static solid colliders for axis-projection checks below.
    // "Static" = has Collider + isSolid, but no Velocity component.
    // Collected once per frame so the inner loop doesn't re-query the registry.
    auto allColliders = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> statics;
    statics.reserve(64);
    for (auto e : allColliders)
    {
        if (!em.registry().all_of<Velocity>(e) && allColliders.get<Collider>(e).isSolid)
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
}
