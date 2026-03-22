#include "systems/RestSpotSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>
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

    for (auto [entity, spot, transform] : reg.view<RestSpot, Transform>().each())
    {
        // Tick cooldown.
        spot.cooldown = std::max(0.0f, spot.cooldown - fdt);

        if (spot.cooldown > 0.0f)
            continue;

        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        if (std::sqrt(dx * dx + dy * dy) > spot.radius)
            continue;

        if (playerHealth.current >= playerHealth.max)
            continue;

        playerHealth.current = playerHealth.max;
        spot.cooldown = kHealCooldown;
        TracyMessageL("RestHeal");
        AudioSystem::playSfx(snd.rest_heal.path, snd.rest_heal.volume);
        ParticleSystem::spawnEmberBurst(em, playerX, playerY, 4);
        std::cout << "[RestSpot] HP restored to " << playerHealth.max << ".\n";
    }
}
