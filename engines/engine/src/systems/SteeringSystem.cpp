#include "systems/SteeringSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Steering constants are read from em.steering_config (set by game-side
// config loading from formulas.json "steering" block).

// Local 2D vector helper. The engine doesn't pull in glm here (this
// file pre-dates the 3D extension) so a small POD type avoids
// introducing a wider dep just for parameter bundling.
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

// Accumulate same-cell crowd repulsion into (crX, crY).
// When the push direction aligns with velocity (entities in a line),
// the push is rotated to perpendicular so trailing entities spread
// sideways instead of being filtered by the dot threshold.
static void applySameCellRepulsion(const FlowField& ff, int ec, int er, const Transform& transform,
                                   float velNormX, float velNormY, float& crX, float& crY)
{
    if (ec < 0 || ec >= FlowField::COLS || er < 0 || er >= FlowField::ROWS)
        return;
    const int ownCount = static_cast<int>(ff.density[er][ec]);
    if (ownCount <= 1)
        return;
    const float cellCX = (static_cast<float>(ec) + 0.5f) * FlowField::CELL_SIZE;
    const float cellCY = (static_cast<float>(er) + 0.5f) * FlowField::CELL_SIZE;
    const float offX = transform.x - cellCX;
    const float offY = transform.y - cellCY;
    const float offLen = std::sqrt(offX * offX + offY * offY);
    if (offLen < 0.5f)
        return;
    float pushX = offX / offLen;
    float pushY = offY / offLen;

    // Forward-aligned push (leading entity): rotate to perpendicular
    // so it spreads sideways instead of accelerating forward.
    // Backward-aligned push (trailing entity): keep as-is — the
    // deceleration creates longitudinal spacing (cushion distance).
    const float alignDot = velNormX * pushX + velNormY * pushY;
    if (alignDot > 0.7f)
    {
        const float cross = velNormX * pushY - velNormY * pushX;
        if (cross >= 0.0f)
        {
            pushX = -velNormY;
            pushY = velNormX;
        }
        else
        {
            pushX = velNormY;
            pushY = -velNormX;
        }
    }

    const float sameWeight = static_cast<float>(ownCount - 1) * 2.0f;
    crX += pushX * sameWeight;
    crY += pushY * sameWeight;
}

// Inputs to the per-entity steering pass. Bundles the entity's
// motion state (velocity-normalized direction, original speed) and
// the steering tunables that the wall + crowd helpers both read.
struct SteeringContext
{
    Vec2 vel_norm; // unit vector along current velocity
    float orig_speed = 0.0f;
    float repulsion_radius = 0.0f;
    float skip_dot_threshold = 0.0f;
    float separation_strength = 0.0f;
};

// Closed-form AABB obstacle: center + half-extents.
struct ObstacleAabb
{
    Vec2 center;
    Vec2 half_extents;
};

// Cross-cell density-grid pass: sample a 5x5 window around the
// entity, accumulate repulsion away from occupied cells weighted by
// density count.
static Vec2 sampleCrowdGridRepulsion(const FlowField& ff, int ec, int er)
{
    static constexpr int CROWD_SAMPLE_RADIUS = 2;
    Vec2 cr{0.0f, 0.0f};
    for (int dr = -CROWD_SAMPLE_RADIUS; dr <= CROWD_SAMPLE_RADIUS; ++dr)
    {
        for (int dc = -CROWD_SAMPLE_RADIUS; dc <= CROWD_SAMPLE_RADIUS; ++dc)
        {
            if (dr == 0 && dc == 0)
                continue;
            const int nc = ec + dc;
            const int nr = er + dr;
            if (nc < 0 || nc >= FlowField::COLS || nr < 0 || nr >= FlowField::ROWS)
                continue;
            const int count = ff.density[nr][nc];
            if (count == 0)
                continue;
            const float rDx = static_cast<float>(-dc);
            const float rDy = static_cast<float>(-dr);
            const float rLen = std::sqrt(rDx * rDx + rDy * rDy);
            cr.x += (rDx / rLen) * static_cast<float>(count);
            cr.y += (rDy / rLen) * static_cast<float>(count);
        }
    }
    return cr;
}

// Strip any crowd-repulsion component pointing INTO the wall normal
// `wallAway`, so crowd separation never pushes an entity toward a
// wall it's already being deflected from. Returns the projected
// direction (unit) or {0,0} if the projection collapses to zero.
static Vec2 stripWallFacingComponent(Vec2 dir, Vec2 wallAway)
{
    const float wallLen = std::sqrt(wallAway.x * wallAway.x + wallAway.y * wallAway.y);
    if (wallLen <= 0.0f)
        return dir;
    const float wnx = wallAway.x / wallLen;
    const float wny = wallAway.y / wallLen;
    const float dot = dir.x * wnx + dir.y * wny;
    if (dot >= 0.0f)
        return dir;
    dir.x -= dot * wnx;
    dir.y -= dot * wny;
    const float pLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (pLen < 0.01f)
        return {0.0f, 0.0f};
    return {dir.x / pLen, dir.y / pLen};
}

// Compute and apply crowd repulsion from the density grid to vel.
// Samples a 5x5 cell window (cross-cell) + same-cell offset pass.
// `wallAway` is the accumulated wall-repulsion normal from the wall
// pass; any component pushing toward that wall is stripped.
static void applyCrowdRepulsion(const FlowField& ff, const Transform& transform, Velocity& vel,
                                const SteeringContext& ctx, Vec2 wallAway)
{
    const int ec = static_cast<int>(transform.x / FlowField::CELL_SIZE);
    const int er = static_cast<int>(transform.y / FlowField::CELL_SIZE);

    Vec2 cr = sampleCrowdGridRepulsion(ff, ec, er);
    applySameCellRepulsion(ff, ec, er, transform, ctx.vel_norm.x, ctx.vel_norm.y, cr.x, cr.y);

    const float crMag = std::sqrt(cr.x * cr.x + cr.y * cr.y);
    if (crMag <= 0.0f)
        return;
    const Vec2 dir = stripWallFacingComponent({cr.x / crMag, cr.y / crMag}, wallAway);
    if (dir.x == 0.0f && dir.y == 0.0f)
        return;

    vel.dx += dir.x * ctx.orig_speed * ctx.separation_strength;
    vel.dy += dir.y * ctx.orig_speed * ctx.separation_strength;
    // Cap at origSpeed -- crowd separation must not accelerate the entity.
    const float crNewMag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    if (crNewMag > ctx.orig_speed)
    {
        vel.dx = (vel.dx / crNewMag) * ctx.orig_speed;
        vel.dy = (vel.dy / crNewMag) * ctx.orig_speed;
    }
}

// Accumulate wall repulsion from a single AABB obstacle.
// Computes closest point on the AABB to the entity, checks distance
// against ctx.repulsion_radius, and deflects perpendicular (slide) or
// pushes away depending on whether the entity is heading toward the
// wall (velDot < ctx.skip_dot_threshold).
static void accumulateWallRepulsion(Vec2 entityPos, const ObstacleAabb& obstacle,
                                    const SteeringContext& ctx, Vec2& rep)
{
    const float cpx = std::clamp(entityPos.x, obstacle.center.x - obstacle.half_extents.x,
                                 obstacle.center.x + obstacle.half_extents.x);
    const float cpy = std::clamp(entityPos.y, obstacle.center.y - obstacle.half_extents.y,
                                 obstacle.center.y + obstacle.half_extents.y);

    const float dx = entityPos.x - cpx;
    const float dy = entityPos.y - cpy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist <= 0.0f || dist >= ctx.repulsion_radius)
        return;

    const float repDirX = dx / dist;
    const float repDirY = dy / dist;

    const float velDotRep = ctx.vel_norm.x * repDirX + ctx.vel_norm.y * repDirY;
    const float weight = (ctx.repulsion_radius - dist) / ctx.repulsion_radius;

    if (velDotRep < ctx.skip_dot_threshold)
    {
        const float cross = ctx.vel_norm.x * repDirY - ctx.vel_norm.y * repDirX;
        rep.x += (cross >= 0.0f ? -repDirY : repDirY) * weight;
        rep.y += (cross >= 0.0f ? repDirX : -repDirX) * weight;
    }
    else
    {
        rep.x += repDirX * weight;
        rep.y += repDirY * weight;
    }
}

void SteeringSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("SteeringSystem");
    const float fdt = static_cast<float>(dt);
    const auto& cfg = em.steering_config;

    // Gather static solid colliders once per frame so the inner loop is a
    // plain array sweep -- no registry queries per entity.
    auto allColliders = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> statics;
    statics.reserve(64);
    for (auto e : allColliders)
    {
        if (!em.registry().all_of<Velocity>(e) && allColliders.get<Collider>(e).is_solid)
            statics.push_back(e);
    }

    for (auto [entity, nav, transform, vel] :
         em.registry().view<NavAgent, Transform, Velocity>().each())
    {
        // Only deflect moving entities. If the entity is stopped there is no
        // meaningful direction to preserve, and renormalizing to zero would
        // produce a divide-by-zero.
        const float origSpeed = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
        if (origSpeed < 1.0f)
        {
            nav.smooth_steer_x = 0.0f;
            nav.smooth_steer_y = 0.0f;
            continue;
        }

        SteeringContext ctx;
        ctx.vel_norm = {vel.dx / origSpeed, vel.dy / origSpeed};
        ctx.orig_speed = origSpeed;
        ctx.repulsion_radius = cfg.repulsion_radius;
        ctx.skip_dot_threshold = cfg.skip_dot_threshold;
        ctx.separation_strength = nav.separation_strength * nav.arrival_scale;
        const Vec2 entityPos{transform.x, transform.y};

        // --- Wall repulsion ------------------------------------------------
        Vec2 rep{0.0f, 0.0f};

        for (auto se : statics)
        {
            const auto& st = allColliders.get<Transform>(se);
            const auto& sc = allColliders.get<Collider>(se);
            const ObstacleAabb obstacle{{st.x, st.y}, {sc.width * 0.5f, sc.height * 0.5f}};
            accumulateWallRepulsion(entityPos, obstacle, ctx, rep);
        }

        // Tile-map wall repulsion: check nearby tiles for non-walkable cells.
        if (em.tile_map.valid())
        {
            const float ts = static_cast<float>(em.tile_map.tile_size);
            const float tileHalf = ts * 0.5f;
            const int col0 =
                static_cast<int>(std::floor((transform.x - cfg.repulsion_radius) / ts));
            const int col1 =
                static_cast<int>(std::floor((transform.x + cfg.repulsion_radius) / ts));
            const int row0 =
                static_cast<int>(std::floor((transform.y - cfg.repulsion_radius) / ts));
            const int row1 =
                static_cast<int>(std::floor((transform.y + cfg.repulsion_radius) / ts));

            for (int tr = row0; tr <= row1; ++tr)
            {
                for (int tc = col0; tc <= col1; ++tc)
                {
                    if (!em.tile_map.in_bounds(tc, tr) || em.tile_map.at(tc, tr).walkable)
                        continue;
                    const ObstacleAabb tileObstacle{{static_cast<float>(tc) * ts + tileHalf,
                                                     static_cast<float>(tr) * ts + tileHalf},
                                                    {tileHalf, tileHalf}};
                    accumulateWallRepulsion(entityPos, tileObstacle, ctx, rep);
                }
            }
        }

        // --- Compute raw steering force (wall + crowd) ---------------------
        float rawSteerX = rep.x * cfg.repulsion_strength;
        float rawSteerY = rep.y * cfg.repulsion_strength;

        // Crowd repulsion: compute into a scratch velocity, extract the
        // delta as the crowd steering contribution.
        if (ctx.separation_strength > 0.0f)
        {
            Velocity crowdVel{vel.dx, vel.dy};
            applyCrowdRepulsion(em.flow_field, transform, crowdVel, ctx, rep);
            rawSteerX += (crowdVel.dx - vel.dx) / origSpeed;
            rawSteerY += (crowdVel.dy - vel.dy) / origSpeed;
        }

        // --- Exponential blend toward raw force ----------------------------
        const float blend = 1.0f - std::exp(-cfg.blend_rate * fdt);
        nav.smooth_steer_x += (rawSteerX - nav.smooth_steer_x) * blend;
        nav.smooth_steer_y += (rawSteerY - nav.smooth_steer_y) * blend;

        // --- Apply smoothed steering to velocity ---------------------------
        vel.dx += nav.smooth_steer_x * origSpeed;
        vel.dy += nav.smooth_steer_y * origSpeed;

        // Renormalize back to the original speed so steering changes
        // direction only, not magnitude.
        const float newMag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
        if (newMag > 0.0f)
        {
            vel.dx = (vel.dx / newMag) * origSpeed;
            vel.dy = (vel.dy / newMag) * origSpeed;
        }
    }
}
