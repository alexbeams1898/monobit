#include "systems/CollisionSystem.h"

#include "ecs/Components.h"

#include <cmath>
#include <vector>

void CollisionSystem::update(EntityManager& em)
{
    em.clearCollisionEvents();

    // Collect every entity that participates in collision.
    // Sort by entity ID so pair-resolution order is deterministic across frames.
    auto view = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> entities(view.begin(), view.end());
    std::sort(entities.begin(), entities.end());

    for (size_t i = 0; i < entities.size(); ++i)
    {
        for (size_t j = i + 1; j < entities.size(); ++j)
        {
            const entt::entity ea = entities[i];
            const entt::entity eb = entities[j];

            auto& ta = view.get<Transform>(ea);
            const auto& ca = view.get<Collider>(ea);
            auto& tb = view.get<Transform>(eb);
            const auto& cb = view.get<Collider>(eb);

            // AABB overlap test (center-based).
            const float halfWA = ca.width * 0.5f;
            const float halfHA = ca.height * 0.5f;
            const float halfWB = cb.width * 0.5f;
            const float halfHB = cb.height * 0.5f;

            const float dx = ta.x - tb.x;
            const float dy = ta.y - tb.y;

            const float overlapX = (halfWA + halfWB) - std::abs(dx);
            const float overlapY = (halfHA + halfHB) - std::abs(dy);

            if (overlapX <= 0.0f || overlapY <= 0.0f)
                continue; // no overlap

            // Emit a collision event for every overlapping pair — gameplay
            // systems (combat, trigger zones) read these each frame.
            em.collisionEvents.push_back({ea, eb});

            // CollisionSystem only corrects dynamic-vs-dynamic pairs (e.g.
            // player colliding with a guard) by splitting the MTV evenly.
            // Static-dynamic correction is handled below in the depenetration pass.
            if (!ca.isSolid || !cb.isSolid)
                continue;

            const bool dynA = em.registry().all_of<Velocity>(ea);
            const bool dynB = em.registry().all_of<Velocity>(eb);

            if (!dynA || !dynB)
                continue; // at least one is static — MovementSystem handles it

            // Both dynamic: resolve with the minimum translation vector (MTV).
            // Split the correction evenly so neither entity dominates.
            float pushX = 0.0f;
            float pushY = 0.0f;

            if (overlapX < overlapY)
                pushX = (dx >= 0.0f) ? overlapX : -overlapX;
            else
                pushY = (dy >= 0.0f) ? overlapY : -overlapY;

            ta.x += pushX * 0.5f;
            ta.y += pushY * 0.5f;
            tb.x -= pushX * 0.5f;
            tb.y -= pushY * 0.5f;
        }
    }

    // Static depenetration pass — correct any dynamic entity pushed into a
    // static wall as a side effect of the dynamic-vs-dynamic resolution above.
    //
    // MovementSystem's velocity projection prevents dynamic entities from
    // entering static solids on their own. However, when two dynamics collide,
    // CollisionSystem moves them directly (bypassing MovementSystem). If a wall
    // is behind either entity the push can land them inside it; MovementSystem
    // then sees a pre-existing overlap and zeroes velocity every frame — entity
    // is permanently stuck.
    //
    // This pass pushes each dynamic entity out of every static solid it now
    // overlaps, on the axis of minimum penetration. The static is never moved.
    for (auto ea : entities)
    {
        if (!em.registry().all_of<Velocity>(ea))
            continue;
        const auto& ca = view.get<Collider>(ea);
        if (!ca.isSolid)
            continue;
        auto& ta = view.get<Transform>(ea);

        for (auto eb : entities)
        {
            if (eb == ea)
                continue;
            if (em.registry().all_of<Velocity>(eb))
                continue; // dynamic — skip, handled by the pair loop above
            const auto& cb = view.get<Collider>(eb);
            if (!cb.isSolid)
                continue;
            const auto& tb = view.get<Transform>(eb);

            const float dx = ta.x - tb.x;
            const float dy = ta.y - tb.y;
            const float overlapX = (ca.width + cb.width) * 0.5f - std::abs(dx);
            const float overlapY = (ca.height + cb.height) * 0.5f - std::abs(dy);

            if (overlapX <= 0.0f || overlapY <= 0.0f)
                continue;

            if (overlapX < overlapY)
                ta.x += (dx >= 0.0f) ? overlapX : -overlapX;
            else
                ta.y += (dy >= 0.0f) ? overlapY : -overlapY;
        }
    }
}
