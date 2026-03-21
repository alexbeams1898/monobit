#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// RestSpotSystem — heals the player to full HP when they step on a RestSpot.
//
// Stub implementation: no animation, no cost, no resource depletion.
// A cooldown prevents the heal from firing every frame while the player
// stands on the spot — it resets when the player re-enters after leaving.
//
// Runs after LevelingSystem so stat changes from a level-up don't
// override the heal this frame.
// ---------------------------------------------------------------------------

class RestSpotSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
