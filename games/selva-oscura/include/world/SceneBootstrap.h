#pragma once

#include "world/Scene.h"

#include <string>

namespace selva::world
{

// Boot the scene system: read assets/scenes/scenes.json, parse each
// listed scene's scene.json, register a JsonScene per entry with the
// engine SceneManager. Returns the SceneId of the default-spawn
// scene (or kInvalidScene on failure).
//
// Must run AFTER engine::physics::initPhysics and AFTER the rest of
// game-side asset modules (Tunables, audio.json, etc.) so scenes can
// reference assets that exist.
engine::world::SceneId loadAllScenes();

// The default-spawn scene id, set by loadAllScenes. Used by new-
// character spawn to know where to put the player.
engine::world::SceneId defaultSpawnScene();

} // namespace selva::world
