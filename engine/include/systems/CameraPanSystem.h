#pragma once

#include "ecs/EntityManager.h"

class CameraPanSystem
{
  public:
    static void update(EntityManager& em, double dt);
};
