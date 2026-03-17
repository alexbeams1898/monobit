#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// PickupSystem — auto-collects nearby Pickup entities for the player.
//
// Pickup.radius is the world-space collection distance.  Any Pickup whose
// centre falls within that radius of the player's centre is collected:
// its XP is added to the player's Experience and the entity is destroyed.
//
// Money drops follow the same auto-collect path.  Inventory items (materials
// for crafting) will require manual pickup — not yet implemented.
//
// Runs after DeathSystem so pickups spawned this frame are already present.
// ---------------------------------------------------------------------------

class PickupSystem
{
  public:
    static void update(EntityManager& em);
};
