#pragma once

#include <entt/entt.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// CollisionEvent — emitted by CollisionSystem each frame for every overlapping
// pair of entities that both carry a Collider.
// Stored in EntityManager so any system can read this frame's collisions without
// being directly coupled to CollisionSystem.
// ---------------------------------------------------------------------------
struct CollisionEvent
{
    entt::entity a; // first entity in the overlapping pair
    entt::entity b; // second entity
};

// ---------------------------------------------------------------------------
// EntityManager — thin owner of the entt::registry.
//
// Responsibilities:
//   - Own the registry (one per game world / scene)
//   - Expose create() / destroy() as the canonical way to manage entity lifetime
//   - Expose registry() for all other operations (views, emplace, get, patch…)
//   - Hold this frame's collision events (written by CollisionSystem, read by others)
//
// Intentionally minimal: entt already has a complete, well-documented API.
// Don't wrap what entt does perfectly well on its own.
//
// JS analogy: think of this as the Redux store — it owns the state and hands
// out a reference. Systems are the reducers that read and write through it.
// ---------------------------------------------------------------------------

class EntityManager
{
  public:
    entt::entity create()
    {
        return registry_.create();
    }

    void destroy(entt::entity entity)
    {
        registry_.destroy(entity);
    }

    entt::registry& registry()
    {
        return registry_;
    }

    const entt::registry& registry() const
    {
        return registry_;
    }

    // Collision events accumulated by CollisionSystem this frame.
    // Cleared at the start of each CollisionSystem::update() call.
    std::vector<CollisionEvent> collisionEvents;

    void clearCollisionEvents()
    {
        collisionEvents.clear();
    }

  private:
    entt::registry registry_;
};
