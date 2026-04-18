#include "systems/ProjectileSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "geom/Intersect.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// Swept hit result when testing the projectile path against one enemy's
// hurtbox set. Holds the smallest t across that entity's shapes plus which
// shape it was (for downstream damage multiplier lookup).
struct EnemyHit
{
    entt::entity entity = entt::null;
    float t = 2.0f;
    int shape_index = -1;
};

// Test the projectile segment (x0,y0)->(x1,y1) against all Hurtbox owners
// other than the projectile's shooter. Returns the earliest hit.
static EnemyHit earliestEnemyHit(EntityManager& em, entt::entity shooter, float x0, float y0,
                                 float x1, float y1, float proj_radius)
{
    EnemyHit best;
    auto& reg = em.registry();
    for (auto [target, tTransform, tHurtbox] : reg.view<Transform, Hurtbox>().each())
    {
        if (target == shooter)
            continue;
        if (!reg.all_of<Health>(target))
            continue;
        if (reg.all_of<Dead>(target))
            continue;

        for (int i = 0; i < static_cast<int>(tHurtbox.shapes.size()); ++i)
        {
            const auto& hs = tHurtbox.shapes[i];
            const auto hit = geom::sweptSegment(x0, y0, x1, y1, proj_radius, hs.shape,
                                                tTransform.x, tTransform.y);
            if (hit.hit && hit.t < best.t)
            {
                best.entity = target;
                best.t = hit.t;
                best.shape_index = i;
            }
        }
    }
    return best;
}

void ProjectileSystem::update(EntityManager& em, float dt)
{
    ZoneScopedN("ProjectileSystem");

    auto& reg = em.registry();

    // Destroy any projectiles that were marked for cleanup last frame. We defer
    // destruction by one tick so DamageSystem has a chance to see the collision
    // event we emitted for them before the entity becomes invalid.
    std::vector<entt::entity> expired;
    for (auto [e, tag] : reg.view<Projectile, PendingDestroy>().each())
    {
        (void)tag;
        expired.push_back(e);
    }
    for (auto e : expired)
        em.destroy(e);

    for (auto [entity, proj, transform, hitbox] : reg.view<Projectile, Transform, Hitbox>().each())
    {
        // Skip projectiles already marked for cleanup so we don't re-emit events
        // for them or move them after they've already hit.
        if (reg.all_of<PendingDestroy>(entity))
            continue;

        const float x0 = transform.x;
        const float y0 = transform.y;
        const float x1 = x0 + proj.dir_x * proj.speed * dt;
        const float y1 = y0 + proj.dir_y * proj.speed * dt;

        const float wall_t = em.tile_map.valid() ? em.tile_map.firstWallHitT(x0, y0, x1, y1) : 1.0f;
        const EnemyHit enemy = earliestEnemyHit(em, proj.owner, x0, y0, x1, y1, proj.radius);

        // Whichever is closer (wall or enemy) wins. Ties favor the enemy so a
        // target pressed against a wall still registers the hit.
        if (enemy.t <= wall_t && enemy.t <= 1.0f)
        {
            transform.x = x0 + enemy.t * (x1 - x0);
            transform.y = y0 + enemy.t * (y1 - y0);
            em.collision_events.push_back({entity, enemy.entity});
            hitbox.hit_shape_index = enemy.shape_index;

            if (proj.pierce_remaining > 0)
                proj.pierce_remaining--;
            else
                reg.emplace_or_replace<PendingDestroy>(entity);
            continue;
        }

        if (wall_t < 1.0f)
        {
            transform.x = x0 + wall_t * (x1 - x0);
            transform.y = y0 + wall_t * (y1 - y0);
            reg.emplace_or_replace<PendingDestroy>(entity);
            continue;
        }

        transform.x = x1;
        transform.y = y1;

        // Range check: destroy if traveled beyond effective range.
        const float dx = transform.x - proj.spawn_x;
        const float dy = transform.y - proj.spawn_y;
        if (dx * dx + dy * dy > proj.max_range * proj.max_range)
            reg.emplace_or_replace<PendingDestroy>(entity);
    }
}
