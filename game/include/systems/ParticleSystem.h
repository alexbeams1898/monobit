#pragma once

#include "ecs/EntityManager.h"

class ParticleSystem
{
  public:
    // Tick particle age, lerp scale, destroy expired particles.
    static void update(EntityManager& em, double dt);

    // Spawn a burst of ember mote particles at (x, y).
    static void spawnEmberBurst(EntityManager& em, float x, float y, int count);
};
