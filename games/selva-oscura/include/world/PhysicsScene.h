#pragma once

#include "physics/PhysicsWorld.h"

#include <glm/vec3.hpp>

#include <cstdio>

namespace selva::world
{

// One-time bring-up of the physics world for the selva-oscura scene.
// Calls engine::physics::initPhysics() and then registers:
//   * each terrain region's mesh as a static trimesh body (Terrain tag)
//   * each chapel static-mesh primitive as a static trimesh body
//     (Architecture tag)
//
// Must run AFTER initTerrain() and initStaticMeshAssets() so the CPU
// vertex/index copies exist on those structs.
// Idempotent. Returns false on failure.
bool initPhysicsScene();

void shutdownPhysicsScene();

// Player body accessor. Lazily created on first call (or by an
// explicit createPlayerBody) so we can fall back to a default
// spawn position. Returns the engine::physics handle so gameplay
// can drive it directly.
engine::physics::BodyHandle playerBody();
engine::physics::BodyHandle createPlayerBody(const glm::vec3& spawn_position);

// One-line dump of init stats (region/primitive/body/triangle counts).
// Caller is responsible for the FILE* lifetime. Used by the F1-toggled
// physics-debug.log writer in PerFrameTick.cpp so init info appears in
// the same file as per-frame state.
void writeInitStatsToLog(FILE* f);

} // namespace selva::world
