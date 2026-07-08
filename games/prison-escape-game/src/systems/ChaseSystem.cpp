#include "systems/ChaseSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/CombatSystem.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

// Format a Tracy message with entity ID context.
// Buffer is static per-call -- safe because Tracy copies immediately.
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
                          float /*px*/, float /*py*/)
{
    static char buf[128]; // NOLINT(concurrency-mt-unsafe) -- single-threaded game loop
    std::snprintf(buf, sizeof(buf), "%s e%u pos=(%.0f,%.0f) vel=(%.0f,%.0f) st=%d slot=%.1f", event,
                  static_cast<unsigned>(entt::to_integral(entity)), tf.x, tf.y, vel.dx, vel.dy,
                  static_cast<int>(ai.state), ai.slot_angle);
    TracyMessage(buf, std::strlen(buf));
}

// ---------------------------------------------------------------------------
// Free helpers -- extracted to keep ChaseSystem::update()'s cognitive
// complexity below the project threshold.  Each represents one AI sub-task.
// ---------------------------------------------------------------------------

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
        fdx = ddx / distOut;
        fdy = ddy / distOut;
    }

    // Per-entity lateral offset: perpendicular to flow direction, stable per ID.
    const int id = static_cast<int>(entt::to_integral(entity));
    const float bias = static_cast<float>((id % 13) - 6) / 6.0f;
    const float perpX = -fdy * bias * chaseSpread;
    const float perpY = fdx * bias * chaseSpread;
    fdx += perpX;
    fdy += perpY;

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

static float wrapAngle(float a)
{
    return std::fmod(std::fmod(a, TWO_PI) + TWO_PI, TWO_PI);
}

// Returns the midpoint of the largest angular gap among existing slots.
static float findBestSlotAngle(entt::registry& reg, entt::entity self, float fallback)
{
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

struct AttackContext
{
    float target_radius;
    float slot_angle;
};

// LoS-gated attack navigation: direct when clear, flow field when blocked.
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

    bool useFlowField = false;
    if (actx.slot_angle != AIController::NO_SLOT)
    {
        if (tile_map.valid() && !tile_map.hasLineOfSight(tf.x, tf.y, goalX, goalY))
            useFlowField = true;
    }
    else
    {
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
    for (const auto h : pool.holders)
        if (h == entity)
            return true;
    return false;
}

static void manageAttackTokens(EntityManager& em, float px, float py, float dt)
{
    auto& pool = em.registry().ctx().get<AttackTokenPool>();

    for (auto [entity, ai] : em.registry().view<AIController>().each())
    {
        if (ai.token_cooldown > 0.0f)
            ai.token_cooldown = std::max(0.0f, ai.token_cooldown - dt);
    }

    const auto removeStart = std::remove_if(pool.holders.begin(), pool.holders.end(),
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
    {
        TracyMessageL("TokenRevoked");
    }
    pool.holders.erase(removeStart, pool.holders.end());

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

static float computeEnemySpeed(EntityManager& em, entt::entity entity, AIController& ai,
                               const FormulaConfig& f, float dt)
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
        speed *= ai.sprint_multiplier;
    return speed;
}

// ---------------------------------------------------------------------------
// Helpers extracted from ChaseSystem::update() to reduce cognitive complexity.
// ---------------------------------------------------------------------------

struct MoveResult
{
    float target_dx = 0.0f;
    float target_dy = 0.0f;
    float dist = 0.0f;
    float arrival_scale = 1.0f;
    bool skip = false;
};

static bool anyAttackersPresent(EntityManager& em)
{
    const auto& pool = em.registry().ctx().get<AttackTokenPool>();
    if (!pool.holders.empty())
        return true;
    for (auto [e, ai] : em.registry().view<AIController>().each())
        if (ai.state == AIController::State::Attack)
            return true;
    return false;
}

static void handleIdleDeceleration(Velocity& vel, const AIController& ai, float fdt)
{
    const float spd = vel.dx * vel.dx + vel.dy * vel.dy;
    if (spd < 0.25f)
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
}

static void assignSlotAngle(entt::registry& reg, entt::entity entity, AIController& ai,
                            const Transform& tf, float px, float py, const TileMap& tile_map,
                            const FormulaConfig& f)
{
    const float bearing = std::atan2(tf.y - py, tf.x - px);
    const float bestAngle = findBestSlotAngle(reg, entity, bearing);
    const float waitR = ai.attack_radius * f.combat_ai.wait_radius_mult;

    float candidate = bestAngle;
    bool assigned = false;
    for (int attempt = 0; attempt < 12 && !assigned; ++attempt)
    {
        float test = bestAngle;
        if (attempt > 0)
        {
            const int half = (attempt + 1) / 2;
            const float offset = f.combat_ai.min_slot_gap * static_cast<float>(half);
            test = ((attempt % 2) == 1) ? bestAngle + offset : bestAngle - offset;
        }
        if (tile_map.valid())
        {
            const float sx = px + std::cos(test) * waitR;
            const float sy = py + std::sin(test) * waitR;
            const int tc = static_cast<int>(sx) / tile_map.tile_size;
            const int tr = static_cast<int>(sy) / tile_map.tile_size;
            if (!tile_map.in_bounds(tc, tr) || !tile_map.at(tc, tr).walkable)
                continue;
        }
        candidate = test;
        assigned = true;
    }
    ai.slot_angle = candidate;
    tracyEntityMsg("SlotAssigned", entity, candidate);
}

// Check whether the world position for a given slot angle is on a walkable tile.
static bool isSlotWalkable(const TileMap& tile_map, float px, float py, float angle, float radius)
{
    if (!tile_map.valid())
        return true;
    const float sx = px + std::cos(angle) * radius;
    const float sy = py + std::sin(angle) * radius;
    const int tc = static_cast<int>(sx) / tile_map.tile_size;
    const int tr = static_cast<int>(sy) / tile_map.tile_size;
    return tile_map.in_bounds(tc, tr) && tile_map.at(tc, tr).walkable;
}

static void rotateSlotIfArrived(AIController& ai, const Transform& tf, float px, float py,
                                const FormulaConfig& f, entt::entity entity, float fdt,
                                const TileMap& tile_map)
{
    const float waitR = ai.attack_radius * f.combat_ai.wait_radius_mult;
    const float slotX = px + std::cos(ai.slot_angle) * waitR;
    const float slotY = py + std::sin(ai.slot_angle) * waitR;
    const float sdx = slotX - tf.x;
    const float sdy = slotY - tf.y;
    const float slotArrDist = f.combat_ai.slot_arrive_dist;
    if (sdx * sdx + sdy * sdy < slotArrDist * slotArrDist)
    {
        const int id = static_cast<int>(entt::to_integral(entity));
        const float rotDir = ((id % 2) == 0) ? 1.0f : -1.0f;
        const float newAngle =
            ai.slot_angle + rotDir * f.combat_ai.slot_rotation_speed * ai.orbit_speed * fdt;
        if (isSlotWalkable(tile_map, px, py, newAngle, waitR))
            ai.slot_angle = newAngle;
    }
}

static MoveResult computeTokenHolderMove(const FlowField& ff, const TileMap& tile_map,
                                         const AIController& ai, const Transform& tf, Velocity& vel,
                                         float px, float py, float speed, const FormulaConfig& f)
{
    MoveResult mr;
    const float ddx = px - tf.x;
    const float ddy = py - tf.y;
    const float distToPlayer = std::sqrt(ddx * ddx + ddy * ddy);
    if (distToPlayer <= ai.attack_radius)
    {
        vel.dx = 0.0f;
        vel.dy = 0.0f;
        mr.arrival_scale = 0.0f;
        return mr;
    }
    const AttackContext actx{ai.attack_radius, ai.slot_angle};
    std::tie(mr.target_dx, mr.target_dy) = computeAttackVelocity(
        ff, tile_map, px, py, actx, tf, speed, mr.arrival_scale, f.combat_ai.attack_arrival_dist);
    mr.arrival_scale = 0.3f;
    return mr;
}

static MoveResult computeWaiterMove(const FlowField& ff, const TileMap& tile_map,
                                    const AIController& ai, const Transform& tf, float px, float py,
                                    float speed, const FormulaConfig& f)
{
    MoveResult mr;
    const float targetRadius = ai.attack_radius * f.combat_ai.wait_radius_mult;

    // If we can't see our slot position, stop and wait for an opening
    // instead of blindly following the flow field toward the player.
    if (ai.slot_angle != AIController::NO_SLOT && tile_map.valid())
    {
        const float goalX = px + std::cos(ai.slot_angle) * targetRadius;
        const float goalY = py + std::sin(ai.slot_angle) * targetRadius;
        if (!tile_map.hasLineOfSight(tf.x, tf.y, goalX, goalY))
            return mr;
    }

    const AttackContext actx{targetRadius, ai.slot_angle};
    std::tie(mr.target_dx, mr.target_dy) = computeAttackVelocity(
        ff, tile_map, px, py, actx, tf, speed * f.combat_ai.waiter_speed_scale, mr.arrival_scale,
        f.combat_ai.attack_arrival_dist);
    return mr;
}

static MoveResult handleAttackState(const FlowField& ff, const TileMap& tile_map,
                                    entt::registry& reg, entt::entity entity, AIController& ai,
                                    const Transform& tf, Velocity& vel, float px, float py,
                                    bool playerFound, bool playerSprinting,
                                    const AttackTokenPool& pool, float speed,
                                    const FormulaConfig& f, float fdt)
{
    MoveResult mr;
    if (!playerFound)
    {
        vel.dx = 0.0f;
        vel.dy = 0.0f;
        mr.skip = true;
        return mr;
    }

    if (playerSprinting)
    {
        std::tie(mr.target_dx, mr.target_dy) = computeChaseVelocity(
            ff, px, py, playerFound, tf, speed, mr.dist, entity, f.combat_ai.chase_spread);
        return mr;
    }

    const bool hasToken = holdsToken(pool, entity);
    const float waitR = ai.attack_radius * f.combat_ai.wait_radius_mult;

    if (ai.slot_angle == AIController::NO_SLOT ||
        !isSlotWalkable(tile_map, px, py, ai.slot_angle, waitR))
        assignSlotAngle(reg, entity, ai, tf, px, py, tile_map, f);
    else if (!hasToken)
        rotateSlotIfArrived(ai, tf, px, py, f, entity, fdt, tile_map);

    if (hasToken)
        return computeTokenHolderMove(ff, tile_map, ai, tf, vel, px, py, speed, f);
    return computeWaiterMove(ff, tile_map, ai, tf, px, py, speed, f);
}

static void blendVelocity(Velocity& vel, float targetDx, float targetDy, float turnSpeed, float fdt)
{
    if (turnSpeed <= 0.0f)
    {
        vel.dx = targetDx;
        vel.dy = targetDy;
    }
    else
    {
        const float blend = 1.0f - std::exp(-turnSpeed * fdt);
        vel.dx += (targetDx - vel.dx) * blend;
        vel.dy += (targetDy - vel.dy) * blend;
    }
}

static void applyChaseArrival(Velocity& vel, float dist, float arrivalRadius, float speed)
{
    if (arrivalRadius <= FlowField::CELL_SIZE || dist >= arrivalRadius)
        return;
    const float scale = dist / arrivalRadius;
    const float curSpeed = std::sqrt(vel.dx * vel.dx + vel.dy * vel.dy);
    if (curSpeed <= 0.0f)
        return;
    const float maxSpeed = speed * scale;
    if (curSpeed > maxSpeed)
    {
        vel.dx = (vel.dx / curSpeed) * maxSpeed;
        vel.dy = (vel.dy / curSpeed) * maxSpeed;
    }
}

static void checkStuck(AIController& ai, entt::entity entity, const Transform& tf,
                       const Velocity& vel, float px, float py)
{
    const float spd = vel.dx * vel.dx + vel.dy * vel.dy;
    if (spd < 1.0f && ai.state != AIController::State::Idle)
    {
        ++ai.stuck_ticks;
        if (ai.stuck_ticks == 60)
            tracyAIDetail("EnemyStuck", entity, tf, vel, ai, px, py);
    }
    else
    {
        ai.stuck_ticks = 0;
    }
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

    if (playerFound && anyAttackersPresent(em))
        manageAttackTokens(em, px, py, fdt);

    const auto& pool = em.registry().ctx().get<AttackTokenPool>();

    for (auto [entity, ai, transform, vel] :
         em.registry().view<AIController, Transform, Velocity>().each())
    {
        if (em.registry().all_of<Dead>(entity))
            continue;

        if (ai.state == AIController::State::Idle && ai.slot_angle != AIController::NO_SLOT)
            ai.slot_angle = AIController::NO_SLOT;

        if (ai.state == AIController::State::Idle)
        {
            handleIdleDeceleration(vel, ai, fdt);
            continue;
        }

        const float speed = computeEnemySpeed(em, entity, ai, f, fdt);
        MoveResult mr;

        if (ai.state == AIController::State::Chase)
        {
            std::tie(mr.target_dx, mr.target_dy) =
                computeChaseVelocity(ff, px, py, playerFound, transform, speed, mr.dist, entity,
                                     f.combat_ai.chase_spread);
        }
        else if (ai.state == AIController::State::Attack)
        {
            mr = handleAttackState(ff, em.tile_map, em.registry(), entity, ai, transform, vel, px,
                                   py, playerFound, playerSprinting, pool, speed, f, fdt);
            if (mr.skip)
                continue;
        }

        blendVelocity(vel, mr.target_dx, mr.target_dy, ai.turn_speed, fdt);

        if (ai.state == AIController::State::Chase && playerFound)
            applyChaseArrival(vel, mr.dist, ai.arrival_radius, speed);

        checkStuck(ai, entity, transform, vel, px, py);

        if (em.registry().all_of<NavAgent>(entity))
            em.registry().get<NavAgent>(entity).arrival_scale = mr.arrival_scale;
    }
}
