#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// DamageSystem — reads CollisionEvents emitted by CollisionSystem and applies
// damage to entities.
//
// Two hit paths handled each frame:
//   1. Hitbox → Health entity: player (or any entity) spawned a Hitbox this
//      frame and it overlaps something with Health.  Computes DEF reduction,
//      stat-requirement penalty, and shield/parry logic before applying.
//   2. AIController(Attack/Chase) → player: enemies that overlap the player
//      while their swing cooldown is ready deal melee damage directly.
//
// After applying damage, if Health.current <= 0, emplaces Dead{} on the entity.
// Dead entities are cleaned up by DeathSystem later in the same frame.
//
// Runs after CollisionSystem so this frame's collisionEvents are populated.
// ---------------------------------------------------------------------------

class DamageSystem
{
  public:
    static void update(EntityManager& em);
};
