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

// Teleport the player to a new world position. If override_yaw is
// true, also sets sPlayer.yaw to `yaw`; otherwise leaves yaw alone
// (seamless world traversal: walking through a door doesn't
// reorient you). Updates the Jolt CharacterVirtual capsule too so
// physics doesn't snap them back.
void teleportPlayerTo(const glm::vec3& world_pos, bool override_yaw, float yaw);

// Handle a scene-transition post-commit. If preserve_pos is true,
// the player's current world position is kept and only Jolt's
// capsule is moved into the new scene (no perceptible jump). If
// false, teleport to `spawn_pos`. Yaw handling follows
// teleportPlayerTo semantics.
void onSceneTransitionCommit(bool preserve_pos, const glm::vec3& spawn_pos, bool override_yaw,
                             float spawn_yaw);

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
float wrapAngleSigned(float delta);

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z).
float yawFromGroundDir(const glm::vec3& dir);

} // namespace selva::gameplay
