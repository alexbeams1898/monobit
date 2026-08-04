#include "Player.h"

#include "Capture.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "systems/CameraSystem.h"

#include <SDL.h>

namespace player
{
namespace
{
constexpr float kSpeed = 160.0f;

entt::entity sPlayer = entt::null;

// WASD / arrows -> a direction. Diagonals are not normalised: this is the
// skeleton, and the feel pass comes with the real movement system.
void readMoveDir(const Uint8* keys, float& dx, float& dy)
{
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        dx -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        dx += 1.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        dy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        dy += 1.0f;
}

// F12 grabs a frame. Edge-triggered, so holding it writes one file and not sixty.
void pollCapture(const Uint8* keys)
{
    static bool sPrev = false;
    const bool down = keys[SDL_SCANCODE_F12] != 0;
    if (down && !sPrev)
        capture::request();
    sPrev = down;
}

// Is the world walkable at this world-space point?
bool walkable(const EntityManager& em, float x, float y)
{
    const TileMap& map = em.tile_map;
    if (map.tile_size <= 0)
        return true;
    const int col = static_cast<int>(x) / map.tile_size;
    const int row = static_cast<int>(y) / map.tile_size;
    if (col < 0 || row < 0 || col >= map.width || row >= map.height)
        return false;
    return map.tiles[static_cast<std::size_t>(row * map.width + col)].walkable;
}
} // namespace

void bind(entt::entity player)
{
    sPlayer = player;
}

void update(Engine& /*engine*/, EntityManager& em, double dt)
{
    if (!em.registry().valid(sPlayer))
        return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    pollCapture(keys);

    float dx = 0.0f;
    float dy = 0.0f;
    readMoveDir(keys, dx, dy);

    auto& t = em.registry().get<Transform>(sPlayer);
    if (dx != 0.0f || dy != 0.0f)
    {
        const float step = kSpeed * static_cast<float>(dt);
        // One axis at a time, so walking into a wall at an angle slides along it
        // instead of stopping dead.
        if (walkable(em, t.x + dx * step, t.y))
            t.x += dx * step;
        if (walkable(em, t.x, t.y + dy * step))
            t.y += dy * step;
    }

    // EVERY tick, not just the moving ones: the engine owns camera-follows-transform,
    // and a camera that only syncs while a key is held starts the game looking at
    // wherever it was default-constructed.
    CameraSystem::update(em);
}

} // namespace player
