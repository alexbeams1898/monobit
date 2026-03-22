#include "systems/ChaseSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

// ---------------------------------------------------------------------------
// Free helpers — extracted to keep ChaseSystem::update()'s cognitive
// complexity below the project threshold.  Each represents one AI state.
// ---------------------------------------------------------------------------

// Chase-state target velocity.
// Uses the flow field exclusively for navigation; falls back to a direct
// vector only when the cell has no BFS path (fully walled off).
// Sets distOut to the current distance to the player (used by arrival
// softening in the outer loop).
static std::pair<float, float> computeChaseVelocity(const FlowField& ff, float px, float py,
                                                    bool playerFound, const Transform& tf,
                                                    float spd, float& distOut)
{
    const int col = static_cast<int>(tf.x / FlowField::CELL_SIZE);
    const int row = static_cast<int>(tf.y / FlowField::CELL_SIZE);
    if (col < 0 || col >= FlowField::COLS || row < 0 || row >= FlowField::ROWS)
        return {0.0f, 0.0f};

    if (!playerFound)
        return {0.0f, 0.0f};

    const float ddx = px - tf.x;
    const float ddy = py - tf.y;
    distOut = std::sqrt(ddx * ddx + ddy * ddy);

    if (distOut <= FlowField::CELL_SIZE)
        return {0.0f, 0.0f};

    const auto& cell = ff.cells[row][col];
    if (cell.dx == 0.0f && cell.dy == 0.0f)
    {
        // Zero flow field: no BFS path reached this cell.
        // Fall back entirely to direct vector — at least the entity drifts
        // toward a wall face and doesn't freeze.
        return {(ddx / distOut) * spd, (ddy / distOut) * spd};
    }

    return {cell.dx * spd, cell.dy * spd};
}

// Attack-state target velocity.
// Uses the flow field for navigation (same as Chase) so obstacles between
// the enemy and the player are routed around rather than charged through.
// Speed is scaled by proximity for a soft arrival at the player.
static std::pair<float, float> computeAttackVelocity(const FlowField& ff, float px, float py,
                                                     const AIController& ai, const Transform& tf,
                                                     float spd)
{
    const float ddx = px - tf.x;
    const float ddy = py - tf.y;
    const float dist = std::sqrt(ddx * ddx + ddy * ddy);

    if (dist < 1.0f)
        return {0.0f, 0.0f};

    const int ffCol = static_cast<int>(tf.x / FlowField::CELL_SIZE);
    const int ffRow = static_cast<int>(tf.y / FlowField::CELL_SIZE);
    float navDx = ddx / dist;
    float navDy = ddy / dist;
    if (ffCol >= 0 && ffCol < FlowField::COLS && ffRow >= 0 && ffRow < FlowField::ROWS)
    {
        const auto& cell = ff.cells[ffRow][ffCol];
        if (cell.dx != 0.0f || cell.dy != 0.0f)
        {
            navDx = cell.dx;
            navDy = cell.dy;
        }
    }

    // Arrival at slot: enemy is on the ring when dist == attack_radius.
    // slotDist measures approach remaining; ramps to 0 at the ring boundary.
    const float slotDist = std::max(0.0f, dist - ai.attack_radius);
    const float scale = std::min(1.0f, slotDist / ai.attack_radius);
    return {navDx * spd * scale, navDy * spd * scale};
}

// Compute base movement speed from DEX, apply sprint multiplier and stamina drain.
static float computeEnemySpeed(EntityManager& em, entt::entity entity, AIController& ai,
                               Transform& transform, const FormulaConfig& f, float dt)
{
    float speed = f.movement.base;
    if (em.registry().all_of<Stats>(entity))
    {
        const int dex = em.registry().get<Stats>(entity).dex;
        speed *=
            (1.0f +
             std::floor(f.movement.dex_scale * std::log(static_cast<float>(dex) + 1.0f)) / 100.0f);
    }

    if (ai.sprint && em.registry().all_of<Stamina>(entity))
    {
        auto& sta = em.registry().get<Stamina>(entity);
        if (sta.current > 0.0f)
        {
            const float wWeight = em.registry().all_of<Weapon>(entity)
                                      ? em.registry().get<Weapon>(entity).weight
                                      : 1.0f;
            const int dexSprint =
                em.registry().all_of<Stats>(entity) ? em.registry().get<Stats>(entity).dex : 1;
            const float drain =
                wWeight * f.stamina.sprint_effort /
                (1.0f + static_cast<float>(dexSprint) * f.stamina.sprint_dex_scale) * dt;
            deductStamina(em.registry(), entity, drain, f);
        }
        else
        {
            ai.sprint = false;
        }
    }

    if (ai.sprint)
    {
        speed *= ai.sprint_multiplier;
        transform.scale = 1.1f;
    }
    else
    {
        transform.scale = 1.0f;
    }
    return speed;
}

void ChaseSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("ChaseSystem");
    const auto& ff = em.flow_field;

    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;
    for (auto e : em.registry().view<PlayerActions>())
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

    auto& f = em.registry().ctx().get<FormulaConfig>();
    const float fdt = static_cast<float>(dt);

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        if (ai.state == AIController::State::Idle)
            continue;

        const float speed = computeEnemySpeed(em, entity, ai, transform, f, fdt);

        float targetDx = 0.0f;
        float targetDy = 0.0f;
        float dist = 0.0f;

        if (ai.state == AIController::State::Chase)
        {
            std::tie(targetDx, targetDy) =
                computeChaseVelocity(ff, px, py, playerFound, transform, speed, dist);
        }
        else if (ai.state == AIController::State::Attack)
        {
            if (!playerFound)
            {
                vel.dx = 0.0f;
                vel.dy = 0.0f;
                continue;
            }
            std::tie(targetDx, targetDy) = computeAttackVelocity(ff, px, py, ai, transform, speed);
        }

        if (ai.turn_speed <= 0.0f)
        {
            vel.dx = targetDx;
            vel.dy = targetDy;
        }
        else
        {
            const float blend = 1.0f - std::exp(-ai.turn_speed * fdt);
            vel.dx += (targetDx - vel.dx) * blend;
            vel.dy += (targetDy - vel.dy) * blend;
        }

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
