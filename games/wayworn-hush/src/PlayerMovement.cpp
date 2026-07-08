#include "PlayerMovement.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <SDL.h>

#include <cmath>

namespace player_movement
{
namespace
{
// Shrink the test box slightly so the character slides past tile corners
// instead of catching on a 1-2px overlap. See prison-escape MovementSystem's
// MOVEMENT_INSET note for the corner-sticking rationale.
constexpr float kInset = 2.0f;

// True if a center-based box (cx,cy,w,h) overlaps any non-walkable tile.
bool touchesSolid(const EntityManager& em, float cx, float cy, float w, float h)
{
    const auto& tm = em.tile_map;
    if (!tm.valid())
        return false;
    const float ts = static_cast<float>(tm.tile_size);
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
    const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
    const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
    const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
    for (int r = rmin; r <= rmax; ++r)
        for (int c = cmin; c <= cmax; ++c)
            if (!tm.in_bounds(c, r) || !tm.at(c, r).walkable)
                return true;
    return false;
}
} // namespace

void update(EntityManager& em, entt::entity player, const unsigned char* keys, float speed,
            float fdt)
{
    auto& reg = em.registry();
    if (!reg.valid(player))
        return;

    auto* t = reg.try_get<Transform>(player);
    auto* v = reg.try_get<Velocity>(player);
    const auto* col = reg.try_get<Collider>(player);
    if (!t || !v || !col)
        return;

    float ix = 0.0f;
    float iy = 0.0f;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
        iy -= 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
        iy += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
        ix -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
        ix += 1.0f;

    // Normalize so diagonal isn't faster than cardinal.
    const float len = std::sqrt(ix * ix + iy * iy);
    if (len > 0.0f)
    {
        ix /= len;
        iy /= len;
    }
    v->dx = ix * speed;
    v->dy = iy * speed;

    const float mw = col->width - kInset;
    const float mh = col->height - kInset;

    // Axis-split projection: resolve X, then Y from the new X. Zeroing the
    // blocked axis lets the character slide along a wall it hits at an angle.
    float nx = t->x + v->dx * fdt;
    if (touchesSolid(em, nx, t->y, mw, mh))
    {
        v->dx = 0.0f;
        nx = t->x;
    }
    float ny = t->y + v->dy * fdt;
    if (touchesSolid(em, nx, ny, mw, mh))
    {
        v->dy = 0.0f;
        ny = t->y;
    }
    t->x = nx;
    t->y = ny;
}
} // namespace player_movement
