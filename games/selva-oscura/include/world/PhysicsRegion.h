#pragma once

#include "physics/PhysicsWorld.h"

#include <glm/vec3.hpp>

#include <cstdio>

namespace selva::world
{

// One-time bring-up of the physics world for the selva-oscura scene.
// Calls engine::physics::initPhysics() and then registers:
//   * each terrain region's mesh as a static trimesh body (Terrain tag)
//
// Chapel + descent + limbo static-mesh primitives are owned by
// JsonRegion (per-region static_meshes array) and registered into
// Jolt at region activation, not here.
//
// Must run AFTER initTerrain() so the CPU vertex/index copies exist.
// Idempotent. Returns false on failure.
bool initPhysicsRegion();

void shutdownPhysicsRegion();

// Player body accessor. Lazily created on first call (or by an
// explicit createPlayerBody) so we can fall back to a default
// spawn position. Returns the engine::physics handle so gameplay
// can drive it directly.
engine::physics::BodyHandle playerBody();
engine::physics::BodyHandle createPlayerBody(const glm::vec3& spawn_position);

// Generic character body creation for any actor (NPC, enemy). Thin
// wrapper around engine::physics::addCharacter that ensures the
// physics world is initialized and applies the actor's collider
// dimensions. Caller stores the returned handle on the Actor and is
// responsible for destroying it on death / cycle reset via
// destroyCharacterBody.
engine::physics::BodyHandle createCharacterBody(const glm::vec3& spawn_position, float radius,
                                                float height);
void destroyCharacterBody(engine::physics::BodyHandle handle);

// One-line dump of init stats (region/primitive/body/triangle counts).
// Caller is responsible for the FILE* lifetime. Used by the F1-toggled
// physics-debug.log writer in PerFrameTick.cpp so init info appears in
// the same file as per-frame state.
void writeInitStatsToLog(FILE* f);

} // namespace selva::world
