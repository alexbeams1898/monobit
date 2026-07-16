#pragma once

#include <entt/entt.hpp>

class EntityManager;

// Free pixel movement for the player (Chrono-Trigger register): WASD sets a
// velocity, which is axis-split-projected against solid tiles so the character
// slides along walls instead of sticking. Runs in the fixed-step update.
namespace player_movement
{
// keys: SDL_GetKeyboardState array. speed: world px/sec. fdt: fixed timestep.
void update(EntityManager& em, entt::entity player, const unsigned char* keys, float speed,
            float fdt);

// Could `entity` stand centered at (wx,wy) -- i.e. is that spot on the map, on walkable
// terrain, and clear of solid props? The same test movement uses to refuse a step, exposed
// so a position that didn't come from walking (a save's resume point, a teleport) can be
// validated against ONE definition of "a place you can be" rather than a second, drifting
// copy. False for an entity with no collider box.
bool canStand(const EntityManager& em, entt::entity entity, float wx, float wy);
} // namespace player_movement
