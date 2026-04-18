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
        // Only damageable entities participate in projectile hits.
        if (!reg.all_of<Health>(target))
            continue;
        // Skip dead targets.
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
    std::vector<entt::entity> toDestroy;

    for (auto [entity, proj, transform, hitbox] : reg.view<Projectile, Transform, Hitbox>().each())
    {
        // Desired end-of-frame position based on stored flight velocity.
        const float x0 = transform.x;
        const float y0 = transform.y;
        const float x1 = x0 + proj.dir_x * proj.speed * dt;
        const float y1 = y0 + proj.dir_y * proj.speed * dt;

        // Projectile radius for swept Minkowski expansion.
        const float proj_radius = proj.radius;

        // Wall hit along the segment (t in [0, 1], or 1.0 if none).
        const float wall_t = em.tile_map.valid() ? em.tile_map.firstWallHitT(x0, y0, x1, y1) : 1.0f;

        // Earliest enemy hurtbox hit along the segment.
        const EnemyHit enemy = earliestEnemyHit(em, proj.owner, x0, y0, x1, y1, proj_radius);

        // Whichever is closer (wall or enemy) wins. Enemy ties with wall at the
        // same t favor enemy (makes shooting into a wall that has an enemy
        // pressed against it still register the enemy hit).
        if (enemy.t <= wall_t && enemy.t <= 1.0f)
        {
            // Teleport the projectile to the intersection point. Emit a
            // collision event so DamageSystem picks up the hit on its next
            // dispatch. Record the hurtbox shape index so DamageSystem can
            // apply dmg_mult.
            transform.x = x0 + enemy.t * (x1 - x0);
            transform.y = y0 + enemy.t * (y1 - y0);
            em.collision_events.push_back({entity, enemy.entity});
            hitbox.hit_shape_index = enemy.shape_index;

            // Pierce handling: continue flight beyond the hit, or destroy.
            if (proj.pierce_remaining > 0)
            {
                proj.pierce_remaining--;
                // Leave the hit_something flag for DamageSystem to reset; we
                // intentionally do NOT clear it here so the damage pass sees
                // the event. DamageSystem is what sets hit_something=true, but
                // the legacy pierce dance that cleared it on re-fire is now
                // obsolete -- a fresh segment next frame starts clean.
            }
            else
            {
                toDestroy.push_back(entity);
                continue;
            }
        }
        else if (wall_t < 1.0f)
        {
            // Wall hit. Stop the projectile at the wall and destroy it.
            transform.x = x0 + wall_t * (x1 - x0);
            transform.y = y0 + wall_t * (y1 - y0);
            toDestroy.push_back(entity);
            continue;
        }
        else
        {
            // Free flight -- no hit this frame.
            transform.x = x1;
            transform.y = y1;
        }

        // Range check: destroy if traveled beyond effective range.
        const float dx = transform.x - proj.spawn_x;
        const float dy = transform.y - proj.spawn_y;
        if (dx * dx + dy * dy > proj.max_range * proj.max_range)
            toDestroy.push_back(entity);
    }

    for (auto e : toDestroy)
        em.destroy(e);
}
