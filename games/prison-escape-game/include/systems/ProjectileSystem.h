#pragma once

#include "ecs/EntityManager.h"

class ProjectileSystem
{
  public:
    static void update(EntityManager& em, float dt);
};
