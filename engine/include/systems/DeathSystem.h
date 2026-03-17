#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// DeathSystem — sweeps entities tagged Dead, spawns loot/XP pickups, and
// destroys the entity.
//
// Runs after DamageSystem.  Must collect all dead entities before calling
// em.destroy() because entt invalidates iterators on destruction.
//
// Player death: logs "Game Over" — no respawn logic yet.
// Enemy death:  spawns a Pickup entity with XP proportional to the enemy's
//               level (from Experience if present, else assumed level 1).
// ---------------------------------------------------------------------------

class DeathSystem
{
  public:
    static void update(EntityManager& em);
};
