#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// DeathSystem — sweeps entities tagged Dead, grants XP to the player, and
// destroys the entity.
//
// Runs after DamageSystem.  Must collect all dead entities before calling
// em.destroy() because entt invalidates iterators on destruction.
//
// Player death: logs "Game Over" — no respawn logic yet.
// Enemy death:  grants XP directly to the player (proportional to the
//               enemy's stat-derived level). No pickup entity.
// ---------------------------------------------------------------------------

class DeathSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
