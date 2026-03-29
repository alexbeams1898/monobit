#pragma once

#include "ecs/EntityManager.h"

class LadderSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
