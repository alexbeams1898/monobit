#pragma once

#include <entt/entt.hpp>

class EntityManager;

// Bare entity spawning shared by the game shell, the area builders, and the
// editor tool -- one place decides what "a thing standing in the world" is.
namespace spawn
{

// A box that reads as a thing, with no art yet: Transform + interpolation
// history + a solid-colour sprite on the character layer.
entt::entity box(EntityManager& em, float x, float y, float size, float r, float g, float b);

} // namespace spawn
