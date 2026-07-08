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
} // namespace player_movement
