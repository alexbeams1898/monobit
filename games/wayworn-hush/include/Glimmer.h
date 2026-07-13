#pragma once

#include "Growth.h"
#include "Observations.h"

#include <string>

#include <entt/entt.hpp>

class EntityManager;

// The world-space glow at each observable: an UNOBSERVED spot glows warm + pulses
// when faced ("notice me"); once OBSERVED it keeps a muted persistent glow. See
// docs/design/OBSERVATION-SYSTEM.md.

// Marks a glow entity as the glimmer for a specific observable. `base_alpha` is
// the smoothed fade-in level (lerped toward the state's steady target); the
// displayed Sprite.alpha = base_alpha + breathing, recomputed each frame so the
// pulse rides ON TOP of a clean fade and never flashes on first appearance.
struct Glimmer
{
    std::string observable_id;
    float base_alpha = 0.0f;
};

namespace glimmer
{
// Spawn one glow entity per observable (alpha starts at 0, below the character).
void spawn(EntityManager& em, const observations::State& obs);

// Per-frame: fade each glimmer's alpha toward its state target (unobserved vs
// observed), with breathing on top. dt = frame seconds; px/py = player pos;
// dir_x/dir_y = facing (`growth` is used only to resolve which spots are faced).
void update(EntityManager& em, const observations::State& obs, const growth::GrowthState& growth,
            float px, float py, float dir_x, float dir_y, float dt);
} // namespace glimmer
