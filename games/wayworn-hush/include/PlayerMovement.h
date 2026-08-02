#pragma once

#include <entt/entt.hpp>

class EntityManager;

// Free pixel movement for the player (Chrono-Trigger register): WASD sets a
// velocity, which is axis-split-projected against solid tiles so the character
// slides along walls instead of sticking. Runs in the fixed-step update.
namespace player_movement
{
// The player's move INTENT this frame: the normalized input direction, before collision.
// (0,0) when no key is held. Distinct from the resulting velocity -- which collision zeroes
// against a wall -- so the animation can show a walk cycle while pressed against something
// the player is still trying to walk into (velocity says stopped; intent says walking).
struct MoveIntent
{
    float dx = 0.0f;
    float dy = 0.0f;
    bool moving() const
    {
        return dx != 0.0f || dy != 0.0f;
    }
};

// keys: SDL_GetKeyboardState array. speed: world px/sec. fdt: fixed timestep.
// `corner_nudge` (world px): how far to look sideways for an opening when blocked dead-on.
// `corner_slide` (0..1): how fast the deflection glide runs, as a fraction of `speed`.
// Returns the frame's move intent (for the animation).
MoveIntent update(EntityManager& em, entt::entity player, const unsigned char* keys, float speed,
                  float corner_nudge, float corner_slide, float fdt);

// Could `entity` stand centered at (wx,wy) -- i.e. is that spot on the map, on walkable
// terrain, and clear of solid props? The same test movement uses to refuse a step, exposed
// so a position that didn't come from walking (a save's resume point, a teleport) can be
// validated against ONE definition of "a place you can be" rather than a second, drifting
// copy. False for an entity with no collider box.
bool canStand(const EntityManager& em, entt::entity entity, float wx, float wy);
} // namespace player_movement
