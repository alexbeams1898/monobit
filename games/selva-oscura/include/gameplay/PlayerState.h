#pragma once

#include "gameplay/Actor.h"

#include <glm/vec3.hpp>

namespace selva::gameplay
{

// PlayerState is just an Actor. The historic separate struct was
// merged into the unified actor pool — the player is now Actor at
// index 0, stored alongside enemies and any future actors in the
// shared pool. This alias keeps existing call sites compiling
// during the migration; new code should refer to Actor directly.
//
// `player()` is declared in gameplay/Actor.h.
using PlayerState = Actor;

// Initialize the player by creating the actor pool with the player
// at index 0. Pool must be empty when called. Idempotent — clears +
// re-creates.
void initPlayer();

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
float wrapAngleSigned(float delta);

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z).
float yawFromGroundDir(const glm::vec3& dir);

} // namespace selva::gameplay
