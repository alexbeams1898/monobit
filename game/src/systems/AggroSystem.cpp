#include "systems/AggroSystem.h"

#include "TileMap.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <tracy/Tracy.hpp>

static void tracyEntityMsg(const char* event, entt::entity entity, float dist = -1.0f)
{
    static char buf[64]; // NOLINT(concurrency-mt-unsafe) -- single-threaded game loop
    if (dist >= 0.0f)
        std::snprintf(buf, sizeof(buf), "%s e%u d=%.0f", event,
                      static_cast<unsigned>(entt::to_integral(entity)), dist);
    else
        std::snprintf(buf, sizeof(buf), "%s e%u", event,
                      static_cast<unsigned>(entt::to_integral(entity)));
    TracyMessage(buf, std::strlen(buf));
}

static void updateSprintFlag(AIController& ai, float distSq, const Stamina* sta)
{
    if (ai.state == AIController::State::Chase && ai.sprint_multiplier > 0.0f &&
        ai.sprint_threshold > 0.0f)
    {
        // Don't sprint inside attack range — save stamina for swings.
        const float minDist =
            (ai.attack_radius > ai.sprint_threshold) ? ai.attack_radius : ai.sprint_threshold;
        const bool inRange = distSq > minDist * minDist;

        if (sta && sta->max_stamina > 0.0f)
        {
            const float ratio = sta->current / sta->max_stamina;
            // Hysteresis: need 50% to start, stop at 20%.
            // Prevents exhaustion stagger → micro-sprint death spiral.
            if (ai.sprint)
                ai.sprint = inRange && ratio > 0.2f;
            else
                ai.sprint = inRange && ratio > 0.5f;
        }
        else
        {
            ai.sprint = inRange;
        }
    }
    else
    {
        ai.sprint = false;
    }
}

void AggroSystem::update(EntityManager& em)
{
    ZoneScopedN("AggroSystem");
    float px = 0.0f;
    float py = 0.0f;
    bool playerSprinting = false;
    bool playerFound = false;

    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            playerFound = true;
            playerSprinting = em.registry().get<PlayerActions>(e).sprint;
        }
        break;
    }

    if (!playerFound)
        return;

    // Sweep all AI entities and manage state transitions.
    // Squared distances used throughout — avoids a sqrt per entity on the hot path.
    //
    // Idle  → Chase  : player enters aggro_radius.
    // Chase → Attack : player enters arrival_radius (entity begins surrounding).
    // Attack → Chase : player exits arrival_radius × 1.2 (hysteresis prevents flicker).
    for (auto [entity, ai, transform] : em.registry().view<AIController, Transform>().each())
    {
        const float dx = px - transform.x;
        const float dy = py - transform.y;
        const float distSq = dx * dx + dy * dy;

        if (ai.state == AIController::State::Idle)
        {
            if (ai.aggro_radius > 0.0f && distSq <= ai.aggro_radius * ai.aggro_radius &&
                (!em.tile_map.valid() ||
                 em.tile_map.hasLineOfSight(transform.x, transform.y, px, py)))
            {
                ai.state = AIController::State::Chase;

                // Snap facing toward player so the enemy doesn't run backward.
                auto* facing = em.registry().try_get<FacingDirection>(entity);
                if (facing)
                {
                    const float dist = std::sqrt(distSq);
                    if (dist > 0.0f)
                    {
                        facing->dx = dx / dist;
                        facing->dy = dy / dist;
                        facing->render_dx = facing->dx;
                        facing->render_dy = facing->dy;
                        facing->aim_dx = facing->dx;
                        facing->aim_dy = facing->dy;
                    }
                }

                tracyEntityMsg("EnemyAggro", entity, std::sqrt(distSq));
            }
        }
        else if (ai.state == AIController::State::Chase)
        {
            // Leash: give up chase if player is too far away.
            if (ai.deaggro_radius > 0.0f && distSq > ai.deaggro_radius * ai.deaggro_radius)
            {
                ai.state = AIController::State::Idle;
                tracyEntityMsg("EnemyDeaggro", entity, std::sqrt(distSq));
                continue;
            }

            // Enter Attack when within arrival radius AND line of sight is clear.
            // Suppress while the player is kiting — enemies stay in Chase (with
            // sprint) and follow via flow field. Battle circle forms when the
            // player stops.
            if (!playerSprinting && ai.attack_radius > 0.0f && ai.arrival_radius > 0.0f &&
                distSq <= ai.arrival_radius * ai.arrival_radius &&
                (!em.tile_map.valid() ||
                 em.tile_map.hasLineOfSight(transform.x, transform.y, px, py)))
            {
                ai.state = AIController::State::Attack;
                tracyEntityMsg("EnemyAttack", entity, std::sqrt(distSq));
            }
        }
        else if (ai.state == AIController::State::Attack)
        {
            // Revert to Chase when the player sprints — enemies need sprint
            // speed to keep up. They'll re-enter Attack once the player
            // stops and they close back into arrival_radius.
            // No distance-based breakoff: slot positions track the player,
            // so Attack-state enemies follow naturally. Deaggro (600px)
            // catches the "player left" case.
            if (playerSprinting)
            {
                ai.state = AIController::State::Chase;
                ai.slot_angle = AIController::NO_SLOT;
                tracyEntityMsg("SprintBreakOff", entity, std::sqrt(distSq));
                continue;
            }
        }

        const Stamina* sta = em.registry().try_get<Stamina>(entity);
        updateSprintFlag(ai, distSq, sta);

        if (em.registry().all_of<FacingDirection>(entity))
            em.registry().get<FacingDirection>(entity).sprinting = ai.sprint;
    }
}
