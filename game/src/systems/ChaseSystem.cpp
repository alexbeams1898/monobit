#include "systems/ChaseSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <tracy/Tracy.hpp>

// Format a Tracy message with entity ID context.
// Buffer is static per-call — safe because Tracy copies immediately.
static void tracyEntityMsg(const char* event, entt::entity entity, float angle = -999.0f)
{
    static char buf[64]; // NOLINT(concurrency-mt-unsafe) -- single-threaded game loop
    if (angle > -900.0f)
        std::snprintf(buf, sizeof(buf), "%s e%u a=%.1f", event,
                      static_cast<unsigned>(entt::to_integral(entity)), angle);
    else
        std::snprintf(buf, sizeof(buf), "%s e%u", event,
                      static_cast<unsigned>(entt::to_integral(entity)));
    TracyMessage(buf, std::strlen(buf));
}

// Log entity position, velocity, and AI state for debugging stuck scenarios.
static void tracyAIDetail(const char* event,
                          entt::entity entity, // NOLINT(readability-function-size)
                          const Transform& tf, const Velocity& vel, const AIController& ai,
                          float px, float py)
{
    static char buf[128]; // NOLINT(concurrency-mt-unsafe) -- single-threaded game loop
    std::snprintf(buf, sizeof(buf), "%s e%u pos=(%.0f,%.0f) vel=(%.0f,%.0f) st=%d slot=%.1f", event,
                  static_cast<unsigned>(entt::to_integral(entity)), tf.x, tf.y, vel.dx, vel.dy,
                  static_cast<int>(ai.state), ai.slot_angle);
    TracyMessage(buf, std::strlen(buf));
}

// ---------------------------------------------------------------------------
// Free helpers — extracted to keep ChaseSystem::update()'s cognitive
// complexity below the project threshold.  Each represents one AI state.
// ---------------------------------------------------------------------------

// Per-entity lateral spread during chase. Read from FormulaConfig.combat_ai.chase_spread.

static std::pair<float, float> computeChaseVelocity(const FlowField& ff, float px, float py,
                                                    bool playerFound, const Transform& tf,
                                                    float spd, float& distOut, entt::entity entity,
                                                    float chaseSpread)
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
    float fdx = cell.dx;
    float fdy = cell.dy;

    if (fdx == 0.0f && fdy == 0.0f)
    {
        // Zero flow field: no BFS path reached this cell.
        // Fall back entirely to direct vector.
        fdx = ddx / distOut;
        fdy = ddy / distOut;
    }

    // Per-entity lateral offset: perpendicular to flow direction, stable per ID.
    // 13 discrete bias values (-6..+6) / 6 = -1.0 to +1.0, prime modulus
    // for uniform distribution across entity IDs.
    const int id = static_cast<int>(entt::to_integral(entity));
    const float bias = static_cast<float>((id % 13) - 6) / 6.0f;
    const float perpX = -fdy * bias * chaseSpread;
    const float perpY = fdx * bias * chaseSpread;
    fdx += perpX;
    fdy += perpY;

    // Renormalize to unit length then scale by speed.
    const float len = std::sqrt(fdx * fdx + fdy * fdy);
    if (len > 0.0f)
    {
        fdx /= len;
        fdy /= len;
    }

    return {fdx * spd, fdy * spd};
}

static constexpr float TWO_PI = 6.28318530f;
static constexpr float PI = 3.14159265f;

// Normalize angle to [0, 2pi).
static float wrapAngle(float a)
{
    return std::fmod(std::fmod(a, TWO_PI) + TWO_PI, TWO_PI);
}

// Find the angle that maximizes spacing from all existing slots.
// Returns the midpoint of the largest angular gap, or fallback if no slots exist.
static float findBestSlotAngle(entt::registry& reg, entt::entity self, float fallback)
{
    // Collect existing slot angles.
    float angles[64]; // NOLINT -- fixed-size, no heap alloc
    int count = 0;
    for (auto [e, ai] : reg.view<AIController>().each())
    {
        if (e != self && ai.slot_angle != AIController::NO_SLOT && count < 64)
            angles[count++] = wrapAngle(ai.slot_angle);
    }
    if (count == 0)
        return fallback;

    std::sort(angles, angles + count);

    // Find the largest arc gap.
    float bestGap = 0.0f;
    float bestMid = fallback;
    for (int i = 0; i < count; ++i)
    {
        const int next = (i + 1) % count;
        const float gap =
            (next == 0) ? (TWO_PI - angles[i] + angles[0]) : (angles[next] - angles[i]);
        if (gap > bestGap)
        {
            bestGap = gap;
            bestMid = angles[i] + gap * 0.5f;
        }
    }
    return bestMid;
}

// Per-entity attack parameters computed from token status.
struct AttackContext
{
    float target_radius; // ring distance to seek
    float slot_angle;    // assigned angle around player (NO_SLOT = direct approach)
};

// Attack-state target velocity.
// Non-holders navigate to their assigned slot position around the player.
// Token holders navigate directly toward the player.
// LoS-gated: direct nav when the path is clear, flow field when a wall blocks it.
static std::pair<float, float> computeAttackVelocity(const FlowField& ff, const TileMap& tile_map,
                                                     float px, float py, const AttackContext& actx,
                                                     const Transform& tf, float spd,
                                                     float& scaleOut, float arrivalDist)
{
    float goalX = px;
    float goalY = py;
    if (actx.slot_angle != AIController::NO_SLOT)
    {
        goalX = px + std::cos(actx.slot_angle) * actx.target_radius;
        goalY = py + std::sin(actx.slot_angle) * actx.target_radius;
    }

    const float dx = goalX - tf.x;
    const float dy = goalY - tf.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 1.0f)
    {
        scaleOut = 0.0f;
        return {0.0f, 0.0f};
    }

    float nx = dx / dist;
    float ny = dy / dist;

    // Decide nav mode: direct when the straight-line path is clear,
    // flow field when a wall blocks it (gets them around obstacles).
    bool useFlowField = false;
    if (actx.slot_angle != AIController::NO_SLOT)
    {
        // Slotted: LoS check to slot position. DDA raycast, ~10-15 tile checks.
        if (tile_map.valid() && !tile_map.hasLineOfSight(tf.x, tf.y, goalX, goalY))
            useFlowField = true;
    }
    else
    {
        // No slot: use flow field when far from goal.
        const float directThreshold = FlowField::CELL_SIZE * 3.0f;
        if (dist > directThreshold)
            useFlowField = true;
    }

    if (useFlowField)
    {
        const int col = static_cast<int>(tf.x / FlowField::CELL_SIZE);
        const int row = static_cast<int>(tf.y / FlowField::CELL_SIZE);
        if (col >= 0 && col < FlowField::COLS && row >= 0 && row < FlowField::ROWS)
        {
            const auto& cell = ff.cells[row][col];
            if (cell.dx != 0.0f || cell.dy != 0.0f)
            {
                nx = cell.dx;
                ny = cell.dy;
            }
        }
    }

    const float speedScale = (dist < arrivalDist) ? dist / arrivalDist : 1.0f;

    scaleOut = speedScale;
    return {nx * spd * speedScale, ny * spd * speedScale};
}

// ---------------------------------------------------------------------------
// Attack token management: limit concurrent attackers, grant to nearest.
// ---------------------------------------------------------------------------

static bool holdsToken(const AttackTokenPool& pool, entt::entity entity)
{
    for (auto h : pool.holders)
        if (h == entity)
            return true;
    return false;
}

static void manageAttackTokens(EntityManager& em, float px, float py, float dt)
{
    auto& pool = em.registry().ctx().get<AttackTokenPool>();

    // 1. Tick token_cooldown for all AI entities.
    for (auto [entity, ai] : em.registry().view<AIController>().each())
    {
        if (ai.token_cooldown > 0.0f)
            ai.token_cooldown = std::max(0.0f, ai.token_cooldown - dt);
    }

    // 2. Clean holders: remove dead, invalid, or non-Attack-state entities.
    auto removeStart = std::remove_if(pool.holders.begin(), pool.holders.end(),
                                      [&](entt::entity e)
                                      {
                                          if (!em.registry().valid(e))
                                              return true;
                                          if (em.registry().all_of<Dead>(e))
                                              return true;
                                          if (!em.registry().all_of<AIController>(e))
                                              return true;
                                          return em.registry().get<AIController>(e).state !=
                                                 AIController::State::Attack;
                                      });
    if (removeStart != pool.holders.end())
        TracyMessageL("TokenRevoked");
    pool.holders.erase(removeStart, pool.holders.end());

    // 3. Grant tokens to nearest eligible entities up to max_tokens.
    while (static_cast<int>(pool.holders.size()) < pool.max_tokens)
    {
        float bestDistSq = std::numeric_limits<float>::max();
        entt::entity best = entt::null;
        for (auto [entity, ai, tf] : em.registry().view<AIController, Transform>().each())
        {
            if (ai.state != AIController::State::Attack)
                continue;
            if (ai.token_cooldown > 0.0f)
                continue;
            if (em.registry().all_of<Dead>(entity))
                continue;
            if (holdsToken(pool, entity))
                continue;
            const float ddx = px - tf.x;
            const float ddy = py - tf.y;
            const float dsq = ddx * ddx + ddy * ddy;
            if (dsq < bestDistSq)
            {
                bestDistSq = dsq;
                best = entity;
            }
        }
        if (best == entt::null)
            break;
        pool.holders.push_back(best);
        tracyEntityMsg("TokenGranted", best);
    }
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

    speed *= ai.speed_multiplier;

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
    bool playerSprinting = false;
    bool playerFound = false;
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& pt = em.registry().get<Transform>(e);
            px = pt.x;
            py = pt.y;
            playerFound = true;
            playerSprinting = em.registry().get<PlayerActions>(e).sprint;
        }
        break;
    }

    auto& f = em.registry().ctx().get<FormulaConfig>();
    const float fdt = static_cast<float>(dt);

    // Only run token management when enemies are in Attack state.
    if (playerFound)
    {
        bool hasAttackers = false;
        const auto& pool = em.registry().ctx().get<AttackTokenPool>();
        if (!pool.holders.empty())
        {
            hasAttackers = true;
        }
        else
        {
            for (auto [e, ai] : em.registry().view<AIController>().each())
            {
                if (ai.state == AIController::State::Attack)
                {
                    hasAttackers = true;
                    break;
                }
            }
        }
        if (hasAttackers)
            manageAttackTokens(em, px, py, fdt);
    }

    const auto& pool = em.registry().ctx().get<AttackTokenPool>();

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        // Release slot only on full disengage (Idle). Preserving through
        // Chase lets the enemy return to the same slot after a brief breakoff
        // instead of thrashing between new assignments.
        if (ai.state == AIController::State::Idle && ai.slot_angle != AIController::NO_SLOT)
            ai.slot_angle = AIController::NO_SLOT;

        if (ai.state == AIController::State::Idle)
        {
            // Decelerate to stop. Hard-zero below 0.5 px/s to prevent
            // asymptotic drift (velocity never reaching exactly zero).
            const float spd = vel.dx * vel.dx + vel.dy * vel.dy;
            if (spd < 0.25f) // 0.5^2
            {
                vel.dx = 0.0f;
                vel.dy = 0.0f;
            }
            else
            {
                const float blend = 1.0f - std::exp(-ai.turn_speed * fdt);
                vel.dx -= vel.dx * blend;
                vel.dy -= vel.dy * blend;
            }
            continue;
        }

        const float speed = computeEnemySpeed(em, entity, ai, transform, f, fdt);

        float targetDx = 0.0f;
        float targetDy = 0.0f;
        float dist = 0.0f;
        float arrivalScale = 1.0f;

        if (ai.state == AIController::State::Chase)
        {
            std::tie(targetDx, targetDy) = computeChaseVelocity(
                ff, px, py, playerFound, transform, speed, dist, entity, f.combat_ai.chase_spread);
        }
        else if (ai.state == AIController::State::Attack)
        {
            if (!playerFound)
            {
                vel.dx = 0.0f;
                vel.dy = 0.0f;
                continue;
            }

            if (playerSprinting)
            {
                std::tie(targetDx, targetDy) =
                    computeChaseVelocity(ff, px, py, playerFound, transform, speed, dist, entity,
                                         f.combat_ai.chase_spread);
            }
            else
            {
                const bool hasToken = holdsToken(pool, entity);

                // All Attack-state enemies keep a slot angle — even token holders.
                // Holders approach from their slot direction at the close ring;
                // non-holders circle at the far ring. This prevents two holders
                // from converging on the same point and walking through each other.
                if (ai.slot_angle == AIController::NO_SLOT)
                {
                    // Place slot in the largest angular gap between existing
                    // slots so enemies distribute evenly around the player.
                    const float bearing = std::atan2(transform.y - py, transform.x - px);
                    const float bestAngle = findBestSlotAngle(em.registry(), entity, bearing);
                    const float waitR = ai.attack_radius * f.combat_ai.wait_radius_mult;

                    // Search near bestAngle, reject wall positions.
                    float candidate = bestAngle;
                    bool assigned = false;
                    for (int attempt = 0; attempt < 12 && !assigned; ++attempt)
                    {
                        float test = bestAngle;
                        if (attempt > 0)
                        {
                            const float offset =
                                f.combat_ai.min_slot_gap * static_cast<float>((attempt + 1) / 2);
                            test = ((attempt % 2) == 1) ? bestAngle + offset : bestAngle - offset;
                        }

                        // Reject if slot position is inside a wall.
                        if (em.tile_map.valid())
                        {
                            const float sx = px + std::cos(test) * waitR;
                            const float sy = py + std::sin(test) * waitR;
                            const int tc = static_cast<int>(sx) / TileMap::TILE_SIZE;
                            const int tr = static_cast<int>(sy) / TileMap::TILE_SIZE;
                            if (!em.tile_map.in_bounds(tc, tr) || !em.tile_map.at(tc, tr).walkable)
                                continue;
                        }

                        candidate = test;
                        assigned = true;
                    }
                    ai.slot_angle = candidate;
                    tracyEntityMsg("SlotAssigned", entity, candidate);
                }
                else if (!hasToken)
                {
                    // Only rotate once close to the current slot.
                    const float slotX = px + std::cos(ai.slot_angle) * ai.attack_radius *
                                                 f.combat_ai.wait_radius_mult;
                    const float slotY = py + std::sin(ai.slot_angle) * ai.attack_radius *
                                                 f.combat_ai.wait_radius_mult;
                    const float sdx = slotX - transform.x;
                    const float sdy = slotY - transform.y;
                    const float slotArrDist = f.combat_ai.slot_arrive_dist;
                    if (sdx * sdx + sdy * sdy < slotArrDist * slotArrDist)
                    {
                        const int id = static_cast<int>(entt::to_integral(entity));
                        const float rotDir = ((id % 2) == 0) ? 1.0f : -1.0f;
                        ai.slot_angle +=
                            rotDir * f.combat_ai.slot_rotation_speed * ai.orbit_speed * fdt;
                    }
                }

                // Holders approach from their slot direction at attack_radius.
                // Non-holders circle at the wider wait ring.
                const float targetRadius =
                    hasToken ? ai.attack_radius : ai.attack_radius * f.combat_ai.wait_radius_mult;

                if (hasToken)
                {
                    // Stop when within attack range.
                    const float ddx = px - transform.x;
                    const float ddy = py - transform.y;
                    const float distToPlayer = std::sqrt(ddx * ddx + ddy * ddy);
                    if (distToPlayer <= ai.attack_radius)
                    {
                        vel.dx = 0.0f;
                        vel.dy = 0.0f;
                        targetDx = 0.0f;
                        targetDy = 0.0f;
                        arrivalScale = 0.0f;
                    }
                    else
                    {
                        AttackContext actx{targetRadius, ai.slot_angle};
                        std::tie(targetDx, targetDy) =
                            computeAttackVelocity(ff, em.tile_map, px, py, actx, transform, speed,
                                                  arrivalScale, f.combat_ai.attack_arrival_dist);
                        arrivalScale = 0.3f;
                    }
                }
                else
                {
                    // Non-holders drift slowly toward their slot.
                    AttackContext actx{targetRadius, ai.slot_angle};
                    std::tie(targetDx, targetDy) =
                        computeAttackVelocity(ff, em.tile_map, px, py, actx, transform,
                                              speed * f.combat_ai.waiter_speed_scale, arrivalScale,
                                              f.combat_ai.attack_arrival_dist);
                }
            }
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
            arrivalScale = dist / ai.arrival_radius;
            const float curSpeed = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
            if (curSpeed > 0.0f)
            {
                const float maxSpeed = speed * arrivalScale;
                if (curSpeed > maxSpeed)
                {
                    vel.dx = (vel.dx / curSpeed) * maxSpeed;
                    vel.dy = (vel.dy / curSpeed) * maxSpeed;
                }
            }
        }

        // Stuck detection: log when an active enemy has near-zero velocity.
        // Rate-limited per entity via stuck_ticks to avoid flooding the trace.
        {
            const float spd = vel.dx * vel.dx + vel.dy * vel.dy;
            if (spd < 1.0f && ai.state != AIController::State::Idle)
            {
                ++ai.stuck_ticks;
                // Log once per second (~60 ticks) to avoid spam.
                if (ai.stuck_ticks == 60)
                    tracyAIDetail("EnemyStuck", entity, transform, vel, ai, px, py);
            }
            else
            {
                ai.stuck_ticks = 0;
            }
        }

        // Publish arrival scale so SteeringSystem can attenuate crowd
        // separation proportionally — prevents repulsion from dominating
        // when the entity is slowing for arrival.
        if (em.registry().all_of<NavAgent>(entity))
            em.registry().get<NavAgent>(entity).arrival_scale = arrivalScale;
    }
}
