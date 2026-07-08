#include "systems/ProjectileSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <tracy/Tracy.hpp>

#include <cmath>
#include <vector>

// Check if a rectangle centered at (cx, cy) with half-extents (hw, hh) overlaps any wall tile.
static bool touchesWall(const EntityManager& em, float cx, float cy, float hw, float hh)
{
    const float ts = static_cast<float>(em.tile_map.tile_size);
    const int cmin = static_cast<int>(std::floor((cx - hw) / ts));
    const int cmax = static_cast<int>(std::floor((cx + hw) / ts));
    const int rmin = static_cast<int>(std::floor((cy - hh) / ts));
    const int rmax = static_cast<int>(std::floor((cy + hh) / ts));
    for (int r = rmin; r <= rmax; ++r)
        for (int c = cmin; c <= cmax; ++c)
            if (em.tile_map.in_bounds(c, r) && !em.tile_map.at(c, r).walkable)
                return true;
    return false;
}

void ProjectileSystem::update(EntityManager& em, float dt)
{
    ZoneScopedN("ProjectileSystem");

    auto& reg = em.registry();
    std::vector<entt::entity> toDestroy;

    for (auto [entity, proj, transform, hitbox] : reg.view<Projectile, Transform, Hitbox>().each())
    {
        // Move the projectile along its stored flight direction.
        const float nx = transform.x + proj.dir_x * proj.speed * dt;
        const float ny = transform.y + proj.dir_y * proj.speed * dt;

        // Wall check: destroy on contact.
        if (em.tile_map.valid())
        {
            const auto* col = reg.try_get<Collider>(entity);
            const float hw = col != nullptr ? col->width * 0.5f : 3.0f;
            const float hh = col != nullptr ? col->height * 0.5f : 3.0f;
            if (touchesWall(em, nx, ny, hw, hh))
            {
                toDestroy.push_back(entity);
                continue;
            }
        }

        transform.x = nx;
        transform.y = ny;

        // Range check: destroy if traveled beyond effective range.
        const float dx = transform.x - proj.spawn_x;
        const float dy = transform.y - proj.spawn_y;
        if (dx * dx + dy * dy > proj.max_range * proj.max_range)
        {
            toDestroy.push_back(entity);
            continue;
        }

        // Hit an enemy (DamageSystem set hit_something via CollisionSystem events).
        if (hitbox.hit_something)
        {
            if (proj.pierce_remaining > 0)
            {
                proj.pierce_remaining--;
                hitbox.hit_something = false;
            }
            else
            {
                toDestroy.push_back(entity);
                continue;
            }
        }
    }

    for (auto e : toDestroy)
        em.destroy(e);
}
