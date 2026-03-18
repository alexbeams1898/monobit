#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// AnimationSystem — advances frame timers and updates Sprite src rects.
//
// Runs every frame before RenderSystem (called from Engine::render with
// wall-clock dt for frame-rate-independent animation timing).
//
// For each entity with Animation + Sprite:
//   1. Resolves animation state from game components (Dead, AttackLocked, etc.)
//   2. Snaps FacingDirection to a 4-directional CardinalDir
//   3. Advances the frame timer and cycles/holds frames
//   4. Writes the computed src rect into Sprite for RenderSystem to draw
// ---------------------------------------------------------------------------

class AnimationSystem
{
  public:
    static void update(EntityManager& em, float dt);
};
