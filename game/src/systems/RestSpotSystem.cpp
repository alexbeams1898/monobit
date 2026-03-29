#include "systems/RestSpotSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/ParticleSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <tracy/Tracy.hpp>

static constexpr float kHealCooldown = 5.0f; // seconds before the spot can heal again

void RestSpotSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("RestSpotSystem");
    const float fdt = static_cast<float>(dt);
    auto& reg = em.registry();

    entt::entity playerEnt = entt::null;
    float playerX = 0.0f;
    float playerY = 0.0f;
    for (auto [entity, actions, transform] : reg.view<PlayerActions, Transform>().each())
    {
        playerEnt = entity;
        playerX = transform.x;
        playerY = transform.y;
        break;
    }

    if (playerEnt == entt::null || !reg.all_of<Health>(playerEnt))
        return;

    auto& playerHealth = reg.get<Health>(playerEnt);

    auto& snd = reg.ctx().get<SoundConfig>();

    auto& ui = reg.ctx().get<UIState>();
    bool playerInAnySpot = false;

    for (auto [entity, spot, transform] : reg.view<RestSpot, Transform>().each())
    {
        // Tick cooldown.
        spot.cooldown = std::max(0.0f, spot.cooldown - fdt);

        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        const bool inRange = std::sqrt(dx * dx + dy * dy) <= spot.radius;

        if (!inRange)
            continue;

        playerInAnySpot = true;

        // Auto-heal on entry (cooldown prevents spamming).
        if (spot.cooldown <= 0.0f && playerHealth.current < playerHealth.max)
        {
            // Block healing if any enemy is aggroed.
            bool inCombat = false;
            for (auto [ai_ent, ai] : reg.view<AIController>().each())
            {
                if (ai.state != AIController::State::Idle)
                {
                    inCombat = true;
                    break;
                }
            }

            if (inCombat)
            {
                spot.cooldown = 1.0f;
                if (!snd.heal_blocked.path.empty())
                    AudioSystem::playSfx(snd.heal_blocked.path, snd.heal_blocked.volume);
            }
            else
            {
                playerHealth.current = playerHealth.max;

                // Restore stamina too.
                if (reg.all_of<Stamina>(playerEnt))
                    reg.get<Stamina>(playerEnt).current =
                        reg.get<Stamina>(playerEnt).max_stamina;

                spot.cooldown = kHealCooldown;
                TracyMessageL("RestHeal");
                if (!snd.rest_heal_paths.empty())
                {
                    static std::mt19937 rng{std::random_device{}()};
                    auto dist = std::uniform_int_distribution<size_t>(
                        0, snd.rest_heal_paths.size() - 1);
                    AudioSystem::playSfx(snd.rest_heal_paths[dist(rng)],
                                         snd.rest_heal.volume);
                }
                else
                {
                    AudioSystem::playSfx(snd.rest_heal.path, snd.rest_heal.volume);
                }
                ParticleSystem::spawnEmberBurst(em, playerX, playerY, 4);
                std::cout << "[RestSpot] HP restored to " << playerHealth.max << ".\n";
            }
        }

        // Open sanctuary menu on interact key or mouse click while in range.
        const auto& actions = reg.get<PlayerActions>(playerEnt);
        if (actions.interact && ui.active_screen == UIState::Screen::None)
            ui.active_screen = UIState::Screen::Sanctuary;

        if (actions.mouse_click && !em.lmb_consumed && ui.active_screen == UIState::Screen::None)
        {
            const float mdx = actions.mouse_world_x - transform.x;
            const float mdy = actions.mouse_world_y - transform.y;
            if (std::sqrt(mdx * mdx + mdy * mdy) <= spot.radius)
            {
                ui.active_screen = UIState::Screen::Sanctuary;
                em.lmb_consumed = true;
                // Remove LMB from event buffer so the menu doesn't see it.
                auto& md = em.mouse_down_events;
                md.erase(std::remove(md.begin(), md.end(), SDL_BUTTON_LEFT), md.end());
            }
        }
    }

    // Auto-close sanctuary menu when player leaves all rest spots.
    if (!playerInAnySpot && ui.active_screen == UIState::Screen::Sanctuary)
        ui.active_screen = UIState::Screen::None;
}
