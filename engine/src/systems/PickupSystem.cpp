#include "systems/PickupSystem.h"

#include "ecs/Components.h"
#include "systems/AudioSystem.h"

#include <cmath>
#include <iostream>
#include <vector>

void PickupSystem::update(EntityManager& em)
{
    auto& reg = em.registry();

    // Find the player (has Input + Transform + Experience).
    entt::entity playerEnt = entt::null;
    float playerX = 0.0f;
    float playerY = 0.0f;

    for (auto [entity, input, transform] : reg.view<Input, Transform>().each())
    {
        playerEnt = entity;
        playerX = transform.x;
        playerY = transform.y;
        break;
    }

    if (playerEnt == entt::null || !reg.all_of<Experience>(playerEnt))
        return;

    auto& playerXP = reg.get<Experience>(playerEnt);

    // Collect pickups within radius.
    std::vector<entt::entity> toCollect;
    for (auto [entity, pickup, transform] : reg.view<Pickup, Transform>().each())
    {
        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        if (std::sqrt(dx * dx + dy * dy) <= pickup.radius)
            toCollect.push_back(entity);
    }

    for (auto e : toCollect)
    {
        const auto& pickup = reg.get<Pickup>(e);
        if (pickup.xp_value > 0)
        {
            playerXP.current_xp += pickup.xp_value;
            AudioSystem::playSfx(em.sounds.pickup.path, em.sounds.pickup.volume);
            std::cout << "[PickupSystem] +" << pickup.xp_value
                      << " XP (total: " << playerXP.current_xp << ")\n";
        }
        if (pickup.money_value > 0)
            std::cout << "[PickupSystem] +$" << pickup.money_value << "\n";

        em.destroy(e);
    }
}
