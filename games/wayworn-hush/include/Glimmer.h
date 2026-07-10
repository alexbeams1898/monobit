#pragma once

#include "Observations.h"

#include <string>

#include <entt/entt.hpp>

class EntityManager;

// The world-space "you can notice this" signal: a soft glow at each observable
// that brightens when the player is near + facing it (and it isn't exhausted),
// and fades otherwise. Minimal but affective -- the world quietly offering
// something. See docs/design/OBSERVATION-SYSTEM.md §8a.

// Marks a glow entity as the glimmer for a specific observable.
struct Glimmer
{
    std::string observable_id;
};

namespace glimmer
{
// Spawn one glow entity per observable (alpha starts at 0, below the character).
void spawn(EntityManager& em, const observations::State& obs);

// Per-frame: drive each glimmer's alpha toward a gentle target based on whether
// the player faces its observable in range and hasn't exhausted it. dt = frame
// seconds; px/py = player pos; dir_x/dir_y = player facing.
void update(EntityManager& em, const observations::State& obs, float px, float py, float dir_x,
            float dir_y, float dt);
} // namespace glimmer
