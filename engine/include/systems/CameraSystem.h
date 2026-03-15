#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// CameraSystem — keeps the Camera component in sync with the player.
//
// Each frame: finds the entity with Transform + Camera + Input (the player),
// and snaps Camera.x/y to that entity's Transform position.
//
// RenderSystem reads the active Camera to compute the view offset applied to
// all world-space draw calls — effectively scrolling the world around the player.
//
// Only entities with all three components (Transform + Camera + Input) are
// tracked. Non-player entities can have a Camera component with active=false
// to disable tracking.
// ---------------------------------------------------------------------------

class CameraSystem
{
  public:
    static void update(EntityManager& em);
};
