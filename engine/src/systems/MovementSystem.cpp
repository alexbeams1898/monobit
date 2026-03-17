#include "systems/MovementSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// How much to shrink the entity's bounding box for movement projection checks.
// Applied as an inset on each side (so a 32x32 entity uses a 30x30 test box).
//
// Why this exists — "corner sticking": without an inset, an entity approaching
// a doorway at a slight angle gets caught on the corner of the adjacent wall
// tile. The entity's full-size box overlaps the tile's edge by 1-2 px on one
// axis, causing that axis's velocity to be zeroed even though the entity is
// clearly trying to slide past the corner. A 2 px inset absorbs that micro-
// overlap, allowing smooth corner slides while still blocking true head-on
// collisions (a 30 px box cannot pass through a 32 px wall).
static constexpr float MOVEMENT_INSET = 2.0f;

// Returns true if two center-based AABBs overlap.
static bool aabbOverlap(float ax, float ay, float aw, float ah, float bx, float by, float bw,
                        float bh)
{
    return std::abs(ax - bx) < (aw + bw) * 0.5f && std::abs(ay - by) < (ah + bh) * 0.5f;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void MovementSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("MovementSystem");
    const float fdt = static_cast<float>(dt);
    const FormulaConfig& f = em.formulas;

    // Pass 0: Stagger lock — zero velocity for any entity that can't move.
    // Must run before Pass 1 (player input) and after ChaseSystem/SteeringSystem
    // (which set enemy velocity earlier this frame) so both are covered.
    for (auto entity : em.registry().view<Staggered, Velocity>())
    {
        auto& vel = em.registry().get<Velocity>(entity);
        vel.dx = 0.0f;
        vel.dy = 0.0f;
    }

    // Pass 1: Input intent → Velocity.
    // Player speed is derived from DEX stat using the formula:
    //   finalSpeed = base * (1 + floor(dex_scale * log(DEX + 1)) / 100)
    // Entities without Stats fall back to f.movement.base (150 px/s default).
    //
    // Skip velocity update if the entity is Dodging (dodge impulse carries through)
    // or Staggered (guard break / parry result locks movement briefly).
    for (auto [entity, input, vel] : em.registry().view<Input, Velocity>().each())
    {
        // Dodging and Staggered states override normal movement.
        if (em.registry().all_of<Dodging>(entity) || em.registry().all_of<Staggered>(entity))
            continue;

        float speed = f.movement.base;
        if (em.registry().all_of<Stats>(entity))
        {
            const int dex = em.registry().get<Stats>(entity).dex;
            speed *= (1.0f +
                      std::floor(f.movement.dex_scale * std::log(static_cast<float>(dex) + 1.0f)) /
                          100.0f);
        }

        vel.dx = input.move_x * speed;
        vel.dy = input.move_y * speed;
    }

    // Pass 2: Velocity → Transform with static wall projection.
    //
    // When the tile map is present, walls are looked up by tile coordinate —
    // O(~4 tile lookups) per axis check regardless of map size. This replaces
    // the old O(n_static) scan over thousands of wall ECS entities.
    //
    // Fall back to ECS statics when no tile map is available (unit tests that
    // set up explicit wall entities without generating a full tile map).
    const bool use_tile_map = em.tile_map.valid();
    const float ts = static_cast<float>(TileMap::TILE_SIZE);

    auto allColliders = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> statics;
    if (!use_tile_map)
    {
        statics.reserve(64);
        for (auto e : allColliders)
        {
            if (!em.registry().all_of<Velocity>(e) && allColliders.get<Collider>(e).is_solid)
                statics.push_back(e);
        }
    }

    for (auto [entity, vel, transform] : em.registry().view<Velocity, Transform>().each())
    {
        const Collider* col = em.registry().try_get<Collider>(entity);
        if (!col)
        {
            // No collider — integrate unconditionally.
            transform.x += vel.dx * fdt;
            transform.y += vel.dy * fdt;
            continue;
        }

        // Inset box dimensions used for movement projection.
        // Slightly smaller than the entity's true collider — see MOVEMENT_INSET.
        const float mw = col->width - MOVEMENT_INSET;
        const float mh = col->height - MOVEMENT_INSET;
        const float hw = mw * 0.5f;
        const float hh = mh * 0.5f;

        // Returns true if the inset box centered at (cx, cy) overlaps any solid wall.
        // Tile map path: O(~4) direct array lookups.
        // ECS fallback path: O(n_statics) AABB scan (for tests without a tile map).
        auto touches_wall = [&](float cx, float cy) -> bool
        {
            if (use_tile_map)
            {
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
            for (auto se : statics)
            {
                const auto& st = allColliders.get<Transform>(se);
                const auto& sc = allColliders.get<Collider>(se);
                if (aabbOverlap(cx, cy, mw, mh, st.x, st.y, sc.width, sc.height))
                    return true;
            }
            return false;
        };

        // X-axis projection: candidate position after horizontal movement.
        float nx = transform.x + vel.dx * fdt;
        if (touches_wall(nx, transform.y))
        {
            vel.dx = 0.0f;
            nx = transform.x;
        }

        // Y-axis projection: candidate position after vertical movement.
        // Uses resolved X (nx) so a corner slide doesn't re-trigger the X wall.
        float ny = transform.y + vel.dy * fdt;
        if (touches_wall(nx, ny))
        {
            vel.dy = 0.0f;
            ny = transform.y;
        }

        transform.x = nx;
        transform.y = ny;
    }

    // Pass 3: Update FacingDirection.
    // Exception: skip while Dodging so the player's facing stays locked during
    // a dodge (facing stays locked so dodge direction doesn't redirect attacks).
    //
    // Player (has Input): use the raw input direction, NOT the post-projection
    // velocity. When pressed against a wall the X or Y velocity axis is zeroed,
    // but the player clearly intends to face that direction — use what they pressed.
    //
    // AI / other entities: use velocity as before (no raw intent signal available).
    for (auto [entity, vel, facing] : em.registry().view<Velocity, FacingDirection>().each())
    {
        if (em.registry().all_of<Dodging>(entity))
            continue;

        float dirX = vel.dx;
        float dirY = vel.dy;

        if (const Input* inp = em.registry().try_get<Input>(entity))
        {
            // Use raw input intent so facing updates even when a wall blocks movement.
            dirX = inp->move_x;
            dirY = inp->move_y;
        }

        const float len = std::sqrt(dirX * dirX + dirY * dirY);
        if (len > 0.0f)
        {
            const float targetX = dirX / len;
            const float targetY = dirY / len;

            // AI entities: blend facing toward the target direction using the
            // same turn_speed that ChaseSystem uses for velocity blending.
            // This prevents "googly eyes" — rapid velocity oscillation from
            // crowd-separation forces would flip facing every frame without
            // smoothing. The player snaps instantly (no AIController).
            if (const AIController* aic = em.registry().try_get<AIController>(entity);
                aic && aic->turn_speed > 0.0f)
            {
                const float blend = 1.0f - std::exp(-aic->turn_speed * fdt);
                facing.dx += (targetX - facing.dx) * blend;
                facing.dy += (targetY - facing.dy) * blend;
                const float fl = std::sqrt(facing.dx * facing.dx + facing.dy * facing.dy);
                if (fl > 0.0f)
                {
                    facing.dx /= fl;
                    facing.dy /= fl;
                }
            }
            else
            {
                facing.dx = targetX;
                facing.dy = targetY;
            }
        }
        // If zero, keep the last known facing direction.
    }
}
