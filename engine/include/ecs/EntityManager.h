#pragma once

#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// EntityManager — thin owner of the entt::registry.
//
// Responsibilities:
//   - Own the registry (one per game world / scene)
//   - Expose create() / destroy() as the canonical way to manage entity lifetime
//   - Expose registry() for all other operations (views, emplace, get, patch…)
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

  private:
    entt::registry registry_;
};
