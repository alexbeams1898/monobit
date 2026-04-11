#include "systems/RestSpotSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/ParticleSystem.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <tracy/Tracy.hpp>

static constexpr float kHealCooldown = 5.0f;  // seconds before the spot can heal again
static constexpr float kSoundInterval = 1.2f; // pause between ambient rest sounds

namespace
{

bool isInCombat(entt::registry& reg)
{
    for (auto [ai_ent, ai] : reg.view<AIController>().each())
    {
        if (ai.state != AIController::State::Idle)
            return true;
    }
    return false;
}

void tryAutoHeal(EntityManager& em, entt::registry& reg, entt::entity playerEnt, float playerX,
                 float playerY, RestSpot& spot, const SoundConfig& snd)
{
    auto& playerHealth = reg.get<Health>(playerEnt);
    if (spot.cooldown > 0.0f || playerHealth.current >= playerHealth.max)
        return;

    if (isInCombat(reg))
    {
        spot.cooldown = 1.0f;
        const auto& blocked = snd.get("heal_blocked");
        if (!blocked.path.empty())
            AudioSystem::playSfx(blocked.path, blocked.volume);
        return;
    }

    playerHealth.current = playerHealth.max;

    // Restore stamina too.
    if (reg.all_of<Stamina>(playerEnt))
        reg.get<Stamina>(playerEnt).current = reg.get<Stamina>(playerEnt).max_stamina;

    spot.cooldown = kHealCooldown;
    TracyMessageL("RestHeal");
    ParticleSystem::spawnEmberBurst(em, playerX, playerY, 4);
}

void tickRestSound(RestSpot& spot, const SoundConfig& snd, float fdt)
{
    spot.sound_timer -= fdt;
    if (spot.sound_timer > 0.0f)
        return;

    const auto& heal = snd.get("rest_heal");
    const auto& vars = heal.variations;
    if (!vars.empty())
    {
        const int idx = spot.sound_index % static_cast<int>(vars.size());
        AudioSystem::playSfx(vars[static_cast<size_t>(idx)], heal.volume);
        ++spot.sound_index;
    }
    else if (!heal.path.empty())
    {
        AudioSystem::playSfx(heal.path, heal.volume);
    }
    spot.sound_timer = kSoundInterval;
}

} // namespace

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

    const auto& snd = reg.ctx().get<SoundConfig>();
    auto& ui = reg.ctx().get<UIState>();
    bool playerInAnySpot = false;

    for (auto [entity, spot, transform] : reg.view<RestSpot, Transform>().each())
    {
        // Tick cooldown.
        spot.cooldown = std::max(0.0f, spot.cooldown - fdt);

        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        const bool inRange = std::sqrt(dx * dx + dy * dy) <= spot.radius;

        if (inRange)
        {
            playerInAnySpot = true;

            // Play sound immediately on entry, then on a timer while present.
            if (!spot.player_present)
            {
                spot.player_present = true;
                spot.sound_timer = 0.0f; // trigger immediately
                spot.sound_index = 0;
            }
            tickRestSound(spot, snd, fdt);

            tryAutoHeal(em, reg, playerEnt, playerX, playerY, spot, snd);

            // Open sanctuary menu on interact key or mouse click while in range.
            const auto& actions = reg.get<PlayerActions>(playerEnt);
            if (actions.interact && ui.active_screen == UIState::Screen::None)
                ui.active_screen = UIState::Screen::Sanctuary;

            if (actions.mouse_click && !em.lmb_consumed &&
                ui.active_screen == UIState::Screen::None)
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
        else
        {
            spot.player_present = false;
        }
    }

    // Auto-close sanctuary menu when player leaves all rest spots.
    if (!playerInAnySpot && ui.active_screen == UIState::Screen::Sanctuary)
        ui.active_screen = UIState::Screen::None;
}
