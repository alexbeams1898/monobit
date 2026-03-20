#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// CameraSystem — keeps Camera components in sync with their entity's Transform.
//
// Each frame: finds entities with Transform + Camera, and snaps Camera.x/y
// to that entity's Transform position when camera.active is true.
//
// RenderSystem reads the active Camera to compute the view offset applied to
// all world-space draw calls.
// ---------------------------------------------------------------------------

class CameraSystem
{
  public:
    static void update(EntityManager& em);
};
