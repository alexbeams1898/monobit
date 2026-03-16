#pragma once

#include "ecs/EntityManager.h"

#include <string>

class SpawnerSystem
{
  public:
    // Reads configPath JSON ("spawns" array of {entity, x, y}), spawns each
    // entity at the given position.  Returns the number successfully spawned.
    static int load(EntityManager& em, const std::string& configPath);
};
