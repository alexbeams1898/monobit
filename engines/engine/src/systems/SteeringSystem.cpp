#include "systems/SteeringSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Steering constants are read from em.steering_config (set by game-side
// config loading from formulas.json "steering" block).

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

// Compute and apply crowd repulsion from the density grid to vel.
// Samples a 5x5 cell window (cross-cell) + same-cell offset pass.
// wallAwayX/Y is the accumulated wall-repulsion normal from the wall pass;
// any crowd-repulsion component that pushes toward a nearby wall is stripped.
static void applyCrowdRepulsion(const FlowField& ff, const Transform& transform, Velocity& vel,
                                float origSpeed, float velNormX, float velNormY,
                                float separation_strength, float wallAwayX, float wallAwayY)
{
    static constexpr int CROWD_SAMPLE_RADIUS = 2;

    const int ec = static_cast<int>(transform.x / FlowField::CELL_SIZE);
    const int er = static_cast<int>(transform.y / FlowField::CELL_SIZE);

    float crX = 0.0f;
    float crY = 0.0f;

    // Cross-cell pass: sample neighbouring cells, repel away from occupied ones.
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
            crX += (rDx / rLen) * static_cast<float>(count);
            crY += (rDy / rLen) * static_cast<float>(count);
        }
    }

    // Same-cell pass: push outward from cell center when density > 1.
    applySameCellRepulsion(ff, ec, er, transform, velNormX, velNormY, crX, crY);

    const float crMag = std::sqrt(crX * crX + crY * crY);
    if (crMag <= 0.0f)
        return;

    float dirX = crX / crMag;
    float dirY = crY / crMag;

    // Strip the wall-facing component so crowd repulsion never pushes toward
    // a wall the entity is already being deflected from.
    const float wallLen = std::sqrt(wallAwayX * wallAwayX + wallAwayY * wallAwayY);
    if (wallLen > 0.0f)
    {
        const float wnx = wallAwayX / wallLen;
        const float wny = wallAwayY / wallLen;
        const float dot = dirX * wnx + dirY * wny;
        if (dot < 0.0f)
        {
            dirX -= dot * wnx;
            dirY -= dot * wny;
            // Re-normalize after projection.
            const float pLen = std::sqrt(dirX * dirX + dirY * dirY);
            if (pLen < 0.01f)
                return;
            dirX /= pLen;
            dirY /= pLen;
        }
    }

    vel.dx += dirX * origSpeed * separation_strength;
    vel.dy += dirY * origSpeed * separation_strength;
    // Cap at origSpeed -- crowd separation must not accelerate the entity.
    const float crNewMag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    if (crNewMag > origSpeed)
    {
        vel.dx = (vel.dx / crNewMag) * origSpeed;
        vel.dy = (vel.dy / crNewMag) * origSpeed;
    }
}

// Accumulate wall repulsion from a single AABB obstacle.
// Computes closest point on the AABB to (ex, ey), checks distance against
// repulsionRadius, and deflects perpendicular (slide) or pushes away depending
// on whether the entity is heading toward the wall (velDot < skipDotThreshold).
static void accumulateWallRepulsion(float ex, float ey, float obstCx, float obstCy, float obstHw,
                                    float obstHh, float velNx, float velNy, float repulsionRadius,
                                    float skipDotThreshold, float& repX, float& repY)
{
    const float cpx = std::clamp(ex, obstCx - obstHw, obstCx + obstHw);
    const float cpy = std::clamp(ey, obstCy - obstHh, obstCy + obstHh);

    const float dx = ex - cpx;
    const float dy = ey - cpy;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist <= 0.0f || dist >= repulsionRadius)
        return;

    const float repDirX = dx / dist;
    const float repDirY = dy / dist;

    const float velDotRep = velNx * repDirX + velNy * repDirY;
    const float weight = (repulsionRadius - dist) / repulsionRadius;

    if (velDotRep < skipDotThreshold)
    {
        const float cross = velNx * repDirY - velNy * repDirX;
        const float perpX = cross >= 0.0f ? -repDirY : repDirY;
        const float perpY = cross >= 0.0f ? repDirX : -repDirX;
        repX += perpX * weight;
        repY += perpY * weight;
    }
    else
    {
        repX += repDirX * weight;
        repY += repDirY * weight;
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

        const float velNormX = vel.dx / origSpeed;
        const float velNormY = vel.dy / origSpeed;

        // --- Wall repulsion ------------------------------------------------
        float repX = 0.0f;
        float repY = 0.0f;

        for (auto se : statics)
        {
            const auto& st = allColliders.get<Transform>(se);
            const auto& sc = allColliders.get<Collider>(se);
            accumulateWallRepulsion(transform.x, transform.y, st.x, st.y, sc.width * 0.5f,
                                    sc.height * 0.5f, velNormX, velNormY, cfg.repulsion_radius,
                                    cfg.skip_dot_threshold, repX, repY);
        }

        // Tile-map wall repulsion: check nearby tiles for non-walkable cells.
        if (em.tile_map.valid())
        {
            const float ts = static_cast<float>(TileMap::TILE_SIZE);
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

                    const float tileCX = static_cast<float>(tc) * ts + tileHalf;
                    const float tileCY = static_cast<float>(tr) * ts + tileHalf;
                    accumulateWallRepulsion(transform.x, transform.y, tileCX, tileCY, tileHalf,
                                            tileHalf, velNormX, velNormY, cfg.repulsion_radius,
                                            cfg.skip_dot_threshold, repX, repY);
                }
            }
        }

        // --- Compute raw steering force (wall + crowd) ---------------------
        float rawSteerX = repX * cfg.repulsion_strength;
        float rawSteerY = repY * cfg.repulsion_strength;

        // Crowd repulsion: compute into a temporary velocity, extract the
        // delta as the crowd steering contribution.
        const float effectiveSeparation = nav.separation_strength * nav.arrival_scale;
        if (effectiveSeparation > 0.0f)
        {
            const float crowdVx = vel.dx;
            const float crowdVy = vel.dy;
            Velocity crowdVel{crowdVx, crowdVy};
            applyCrowdRepulsion(em.flow_field, transform, crowdVel, origSpeed, velNormX, velNormY,
                                effectiveSeparation, repX, repY);
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
