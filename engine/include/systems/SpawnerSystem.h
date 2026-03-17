#pragma once

#include "ecs/EntityManager.h"

#include <string>

class SpawnerSystem
{
  public:
    // Reads configPath JSON ("spawns" array of {entity, x, y}), spawns each
    // entity at the given position.  Returns the number successfully spawned.
    static int load(EntityManager& em, const std::string& configPath);

    // Timed wave spawner — call every frame with dt in seconds.
    // Every spawnInterval seconds, spawns one enemy at a random position
    // spawnDistance pixels from the player, just outside the camera view.
    static void update(EntityManager& em, double dt);
};
