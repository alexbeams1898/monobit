#pragma once

#include "ecs/EntityManager.h"

// ---------------------------------------------------------------------------
// CollisionSystem — AABB overlap detection and solid-entity resolution.
//
// Each frame, update() does three things:
//   1. Clears last frame's collision events from EntityManager.
//   2. Tests every pair of (Transform + Collider) entities for AABB overlap.
//   3. For each overlapping pair:
//        a. Emits a CollisionEvent into EntityManager::collisionEvents so other
//           systems can react without coupling to CollisionSystem directly.
//        b. If both entities are solid and at least one is dynamic (has Velocity),
//           pushes the dynamic entity out using the minimum-translation vector
//           (MTV) — push along the axis with the smaller penetration depth.
//
// Static entity = has Collider but no Velocity. Immovable.
// Dynamic entity = has Collider + Velocity. Can be pushed.
// Two dynamics colliding = displacement split 50/50.
// ---------------------------------------------------------------------------

class CollisionSystem
{
  public:
    static void update(EntityManager& em);
};
