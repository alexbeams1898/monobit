#pragma once

#include <glm/vec3.hpp>

#include <vector>

namespace selva::combat
{

// One floating damage number. Spawned at hit time at the contact
// world position, drifts upward, fades over its lifetime, despawns
// when lifetime reaches zero.
struct DamageNumber
{
    glm::vec3 world_pos{0.0f}; // updated each frame: base + drift_velocity * elapsed
    glm::vec3 origin{0.0f};    // spawn position (never modified)
    int amount = 0;
    float elapsed = 0.0f;
    float lifetime = 1.0f;
    bool crit = false; // future: region multiplier, status, etc.
};

// Spawn a floating damage number at the given world position.
// Called by the hit-event handler.
void spawnDamageNumber(const glm::vec3& world_pos, int amount, bool crit = false);

// Advance lifetimes, expire any past their lifetime. Call once per
// frame from gameplay.
void tickDamageNumbers(float dt);

// Read-only access for the renderer.
const std::vector<DamageNumber>& damageNumbers();

} // namespace selva::combat
