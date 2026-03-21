#pragma once

#include "ecs/EntityManager.h"

// InputMappingSystem -- maps raw SDL keyboard state to PlayerActions fields.
// Must run first in the game update.
class InputMappingSystem
{
  public:
    static void update(EntityManager& em);
};
