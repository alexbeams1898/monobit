#include "systems/SteeringSystem.h"

#include "ecs/Components.h"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>
#include <vector>

// Distance from wall surface (px) at which repulsion starts.
// At 20 px, the force kicks in before the entity enters the clearance zone
// (~16 px from wall surface) and well before MovementSystem would freeze it.
// Increase to push entities further from walls; decrease for tighter corridors.
static constexpr float REPULSION_RADIUS = 20.0f;

// How hard the repulsion deflects the velocity.
// At REPULSION_STRENGTH=0.5 and zero proximity (weight=1.0), a lateral wall
// deflects direction by arctan(0.5) ≈ 27°. At REPULSION_RADIUS distance
// (weight→0) the force fades to zero.
//
// Must stay < 1.0: at REPULSION_STRENGTH ≥ 1.0 a wall behind or beside the
// entity can push velocity past zero and flip its direction — causing the
// entity to bounce back and forth rather than steer past the wall.
// Increase for sharper avoidance; decrease for more gradual steering.
static constexpr float REPULSION_STRENGTH = 0.5f;

// Dot-product threshold for skipping a force contribution.
//
// Applied to both wall repulsion and crowd separation:
//   dot = -1.0 → force exactly opposes velocity (entity dead ahead) → skip
//   dot =  0.0 → force is perpendicular (entity to the side)        → apply
//   dot = +1.0 → force aligns with velocity (entity behind)         → apply
//
// -0.5 is cos(120°): forces are skipped only when the source is within a
// 60° cone in front of the entity. Sources outside that cone — including
// those diagonally ahead — still contribute lateral steering.
//
// For walls: prevents head-on repulsion that fights MovementSystem.
// For enemies: prevents same-direction enemies from oscillating against
// each other (e.g. two enemies converging east/west fight each other's
// approach and bounce). CollisionSystem handles actual can't-overlap.
static constexpr float SKIP_DOT_THRESHOLD = -0.5f;

void SteeringSystem::update(EntityManager& em)
{
    ZoneScopedN("SteeringSystem");
    // Gather static solid colliders once per frame so the inner loop is a
    // plain array sweep — no registry queries per entity.
    auto allColliders = em.registry().view<Transform, Collider>();
    std::vector<entt::entity> statics;
    statics.reserve(64);
    for (auto e : allColliders)
    {
        if (!em.registry().all_of<Velocity>(e) && allColliders.get<Collider>(e).is_solid)
            statics.push_back(e);
    }

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        if (ai.state == AIController::State::Idle)
            continue;

        // Only deflect moving entities.  If the entity is stopped there is no
        // meaningful direction to preserve, and renormalizing to zero would
        // produce a divide-by-zero.
        const float origSpeed = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
        if (origSpeed < 1.0f)
            continue;

        const float velNormX = vel.dx / origSpeed;
        const float velNormY = vel.dy / origSpeed;

        // --- Wall repulsion ------------------------------------------------
        float repX = 0.0f;
        float repY = 0.0f;

        for (auto se : statics)
        {
            const auto& st = allColliders.get<Transform>(se);
            const auto& sc = allColliders.get<Collider>(se);

            // Closest point on the wall AABB to the entity center.
            const float hw = sc.width * 0.5f;
            const float hh = sc.height * 0.5f;
            const float cx = std::clamp(transform.x, st.x - hw, st.x + hw);
            const float cy = std::clamp(transform.y, st.y - hh, st.y + hh);

            // Vector from that closest point to the entity center.
            // Direction = "away from wall" when entity is outside the wall.
            const float dx = transform.x - cx;
            const float dy = transform.y - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);

            if (dist <= 0.0f || dist >= REPULSION_RADIUS)
                continue;

            const float repDirX = dx / dist;
            const float repDirY = dy / dist;

            // Skip walls that are roughly in front — their repulsion would
            // oppose forward momentum and cause bouncing. MovementSystem
            // already handles head-on wall contact via axis projection.
            const float velDotRep = velNormX * repDirX + velNormY * repDirY;
            if (velDotRep < SKIP_DOT_THRESHOLD)
                continue;

            // Linear falloff: weight = 1 when touching, 0 at REPULSION_RADIUS.
            const float weight = (REPULSION_RADIUS - dist) / REPULSION_RADIUS;
            repX += repDirX * weight;
            repY += repDirY * weight;
        }

        if (repX != 0.0f || repY != 0.0f)
        {
            // Scale repulsion by the entity's current speed (same units as velocity)
            // and blend it into the velocity. This changes the direction without the
            // caller needing to know the raw magnitude of the repulsion vector.
            vel.dx += repX * origSpeed * REPULSION_STRENGTH;
            vel.dy += repY * origSpeed * REPULSION_STRENGTH;

            // Renormalize back to the original speed so repulsion changes
            // direction only, not magnitude.
            const float newMag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
            if (newMag > 0.0f)
            {
                vel.dx = (vel.dx / newMag) * origSpeed;
                vel.dy = (vel.dy / newMag) * origSpeed;
            }
        }

        // --- Crowd repulsion -----------------------------------------------
        // Read the enemy density grid (populated by FlowFieldSystem this frame)
        // and steer away from cells with high occupancy.  Enemies spread into a
        // natural ring rather than piling into a solid glob.
        //
        // Two passes:
        //   Cross-cell — sample the 5×5 window of neighbouring cells, repel
        //     away from occupied ones.  O(1) per entity (fixed 25-cell window).
        //   Same-cell — when density[ec][er] > 1, another enemy shares this
        //     exact 16 px cell.  "Repel away from cell offset" has no direction
        //     from the grid, so use the entity's own within-cell offset as the
        //     outward push: each enemy's micro-position gives a unique spread
        //     direction, separating co-located enemies toward different cell
        //     edges without any pairwise distance check (O(n) total).
        //
        // Both passes apply SKIP_DOT_THRESHOLD: forces directly opposing
        // forward motion are skipped to prevent oscillation (two enemies
        // converging along the same axis would push each other back and
        // forth without it). CollisionSystem handles actual overlap.
        //
        // Cap at origSpeed (not renorm): lateral crowd deflects direction
        // (speed preserved); head-on crowd slows the entity — correct, since
        // you naturally slow pressing into a crowd.
        //
        // separation_strength = 0 → disabled; 0.6 = CO; 1.2 = warden.
        if (ai.separation_strength > 0.0f)
        {
            static constexpr int CROWD_SAMPLE_RADIUS = 2;

            const auto& ff = em.flow_field;
            const int ec = static_cast<int>(transform.x / FlowField::CELL_SIZE);
            const int er = static_cast<int>(transform.y / FlowField::CELL_SIZE);
            float crX = 0.0f;
            float crY = 0.0f;

            // Cross-cell pass.
            for (int dr = -CROWD_SAMPLE_RADIUS; dr <= CROWD_SAMPLE_RADIUS; ++dr)
            {
                for (int dc = -CROWD_SAMPLE_RADIUS; dc <= CROWD_SAMPLE_RADIUS; ++dc)
                {
                    if (dr == 0 && dc == 0)
                        continue; // own cell handled in same-cell pass below
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

            // Same-cell pass — enemies sharing this 16 px cell.
            // Use within-cell offset as the unique outward direction per entity.
            // Weighted 2× an adjacent cell (closer = stronger) and filtered by
            // SKIP_DOT_THRESHOLD so it can't cause oscillation when the offset
            // happens to point opposite to forward motion.
            if (ec >= 0 && ec < FlowField::COLS && er >= 0 && er < FlowField::ROWS)
            {
                const int ownCount = static_cast<int>(ff.density[er][ec]);
                if (ownCount > 1)
                {
                    const float cellCX = (static_cast<float>(ec) + 0.5f) * FlowField::CELL_SIZE;
                    const float cellCY = (static_cast<float>(er) + 0.5f) * FlowField::CELL_SIZE;
                    const float offX = transform.x - cellCX;
                    const float offY = transform.y - cellCY;
                    const float offLen = std::sqrt(offX * offX + offY * offY);
                    if (offLen >= 0.5f)
                    {
                        const float offNX = offX / offLen;
                        const float offNY = offY / offLen;
                        // Skip if the offset direction opposes forward motion —
                        // same rule as walls and cross-cell crowd.
                        if (velNormX * offNX + velNormY * offNY >= SKIP_DOT_THRESHOLD)
                        {
                            const float sameWeight = static_cast<float>(ownCount - 1) * 2.0f;
                            crX += offNX * sameWeight;
                            crY += offNY * sameWeight;
                        }
                    }
                }
            }

            const float crMag = std::sqrt(crX * crX + crY * crY);
            if (crMag > 0.0f)
            {
                vel.dx += (crX / crMag) * origSpeed * ai.separation_strength;
                vel.dy += (crY / crMag) * origSpeed * ai.separation_strength;
                // Cap at origSpeed — crowd separation must not accelerate the
                // entity beyond its configured speed.  Below origSpeed is fine:
                // an entity pressing head-on into a crowd naturally slows down.
                const float crNewMag = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
                if (crNewMag > origSpeed)
                {
                    vel.dx = (vel.dx / crNewMag) * origSpeed;
                    vel.dy = (vel.dy / crNewMag) * origSpeed;
                }
            }
        }
    }
}
