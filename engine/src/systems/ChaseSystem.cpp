#include "systems/ChaseSystem.h"

#include "ecs/Components.h"

#include <algorithm>
#include <cmath>

void ChaseSystem::update(EntityManager& em, double dt)
{
    // Read the flow field built this frame by FlowFieldSystem.
    // Each cell contains a pre-normalized direction toward the player along the
    // shortest open path. O(1) lookup per enemy — no per-frame pathfinding.
    const auto& ff = em.flowField;

    // Cache player position — used for stop-condition math and direct-vector fallback.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;
    for (auto e : em.registry().view<Input>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& pt = em.registry().get<Transform>(e);
            px = pt.x;
            py = pt.y;
            playerFound = true;
        }
        break;
    }

    const FormulaConfig& f = em.formulas;

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        if (ai.state == AIController::State::Idle)
            continue;

        // Derive movement speed from DEX — same formula as the player in MovementSystem.
        // Entities without Stats use the base speed directly.
        float speed = f.movement.base;
        if (em.registry().all_of<Stats>(entity))
        {
            const int dex = em.registry().get<Stats>(entity).dex;
            speed *= (1.0f +
                      std::floor(f.movement.dex_scale * std::log(static_cast<float>(dex) + 1.0f)) /
                          100.0f);
        }

        float targetDx = 0.0f;
        float targetDy = 0.0f;
        float dist = 0.0f; // distance to player — used in both branches

        if (ai.state == AIController::State::Chase)
        {
            const int col = static_cast<int>(transform.x / FlowField::CELL_SIZE);
            const int row = static_cast<int>(transform.y / FlowField::CELL_SIZE);

            // Entities outside the grid get no movement — safe default.
            if (col < 0 || col >= FlowField::COLS || row < 0 || row >= FlowField::ROWS)
            {
                vel.dx = 0.0f;
                vel.dy = 0.0f;
                continue;
            }

            const auto& cell = ff.cells[row][col];

            if (!playerFound)
            {
                // No player — stay still.
            }
            else
            {
                const float ddx = px - transform.x;
                const float ddy = py - transform.y;
                dist = std::sqrt(ddx * ddx + ddy * ddy);

                if (dist <= FlowField::CELL_SIZE)
                {
                    // At the player's cell — stop.
                }
                else if (cell.dx == 0.0f && cell.dy == 0.0f)
                {
                    // Zero flow field: no BFS path reached this cell (e.g. fully
                    // walled off). Fall back entirely to direct vector — at least
                    // the entity drifts toward a wall face and doesn't freeze.
                    targetDx = (ddx / dist) * speed;
                    targetDy = (ddy / dist) * speed;
                }
                else
                {
                    // Use the flow field exclusively.
                    //
                    // A previous version blended in the direct vector at long range
                    // (dist > DIRECT_CHASE_FAR) for smoother far-range tracking. This
                    // caused enemies to charge through walls when the player was on the
                    // other side: at long range directWeight reached 1.0, overriding
                    // flow field routing. The flowDotDirect suppression (dot ≤ 0) only
                    // caught opposite-direction cases — not near-perpendicular ones.
                    // Example: flow=south (routing around right wall), direct=east-ish
                    // (player far east), dot = +0.14 → blend fires → enemy charges east
                    // into right wall and freezes permanently.
                    //
                    // The flow field routes correctly in all cases. Velocity blending
                    // below handles smooth direction transitions at cell boundaries.
                    // Direct vector is kept only as a fallback for unreachable cells
                    // (zero flow field above).
                    targetDx = cell.dx * speed;
                    targetDy = cell.dy * speed;
                }
            }
        }
        else if (ai.state == AIController::State::Attack)
        {
            if (!playerFound)
            {
                vel.dx = 0.0f;
                vel.dy = 0.0f;
                continue;
            }

            // Slot = point on the attack ring directly in this entity's current
            // direction from the player.  Each entity approaches from a different
            // angle, so each targets a unique point on the ring — surrounding
            // emerges naturally without explicit slot assignment.
            //
            // As the player moves, the slot moves with them.  The entity chases
            // its (moving) slot, orbiting with the player at attack range.
            // Transitions back to Chase (via AggroSystem) if the player runs away.
            const float toDx = transform.x - px;
            const float toDy = transform.y - py;
            dist = std::sqrt(toDx * toDx + toDy * toDy);
            const float invDist = (dist > 0.0f) ? 1.0f / dist : 0.0f;
            const float slotX = px + toDx * invDist * ai.attack_radius;
            const float slotY = py + toDy * invDist * ai.attack_radius;

            const float dsx = slotX - transform.x;
            const float dsy = slotY - transform.y;
            const float slotDist = std::sqrt(dsx * dsx + dsy * dsy);

            if (slotDist <= FlowField::CELL_SIZE)
            {
                // At the slot — hold position.
                targetDx = 0.0f;
                targetDy = 0.0f;
            }
            else
            {
                // Soft arrival at slot: speed scales linearly from full at
                // attack_radius distance down to zero at CELL_SIZE.  The entity
                // drifts smoothly onto the ring and holds position with the player
                // as they move; it never fully freezes unless the player stops.
                const float scale = std::min(1.0f, slotDist / ai.attack_radius);
                targetDx = (dsx / slotDist) * speed * scale;
                targetDy = (dsy / slotDist) * speed * scale;
            }
        }

        // Velocity blending — smooths direction changes at cell boundaries and
        // softens the Chase → Attack transition.  Same exponential lerp for both
        // states:
        //   blend = 1 − exp(−turn_speed × dt) ≈ turn_speed × dt for small dt
        // At 60 Hz with turn_speed=8: blend ≈ 0.13 → visibly smooth but still
        // responsive. Set turn_speed=0 for instant snap (legacy / debug).
        if (ai.turn_speed <= 0.0f)
        {
            vel.dx = targetDx;
            vel.dy = targetDy;
        }
        else
        {
            const float blend = 1.0f - std::exp(-ai.turn_speed * static_cast<float>(dt));
            vel.dx += (targetDx - vel.dx) * blend;
            vel.dy += (targetDy - vel.dy) * blend;
        }

        // Arrival softening — Chase state only.
        //
        // Applying this to targetDx/Dy BEFORE blending doesn't work: at
        // turn_speed=4 the blend factor is ~6% per frame at 60 Hz, so actual
        // velocity barely tracks the softened target before the enemy crosses
        // the zone. The effect is imperceptible regardless of arrival_radius.
        //
        // Post-blend cap is immediate: maxSpeed = speed * (dist/arrival_radius).
        // At the edge of the zone: maxSpeed = speed (no reduction).
        // At dist=0:               maxSpeed = 0 (fully stopped).
        //
        // Attack state has its own per-slot arrival scaling above.
        if (ai.state == AIController::State::Chase && playerFound &&
            ai.arrival_radius > FlowField::CELL_SIZE && dist < ai.arrival_radius)
        {
            const float curSpeed = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
            if (curSpeed > 0.0f)
            {
                const float maxSpeed = speed * (dist / ai.arrival_radius);
                if (curSpeed > maxSpeed)
                {
                    vel.dx = (vel.dx / curSpeed) * maxSpeed;
                    vel.dy = (vel.dy / curSpeed) * maxSpeed;
                }
            }
        }
    }
}
