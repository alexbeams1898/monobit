#include "systems/LadderSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/WaveSystem.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>

void LadderSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("LadderSystem");
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

    if (playerEnt == entt::null)
        return;

    for (auto [entity, ladder, transform] : reg.view<Ladder, Transform>().each())
    {
        // Spawn animation: scale up from 0 to 1.
        if (ladder.spawning)
        {
            ladder.spawn_timer = std::min(ladder.spawn_timer + fdt, ladder.spawn_duration);
            transform.scale = ladder.spawn_timer / ladder.spawn_duration;
            if (ladder.spawn_timer >= ladder.spawn_duration)
            {
                transform.scale = 1.0f;
                ladder.spawning = false;
            }
            continue;
        }

        // Interaction: player in range + interact key -> descend.
        const float dx = transform.x - playerX;
        const float dy = transform.y - playerY;
        if (std::sqrt(dx * dx + dy * dy) > ladder.radius)
            continue;

        const auto& actions = reg.get<PlayerActions>(playerEnt);
        if (actions.interact)
        {
            const auto& sc = reg.ctx().get<SoundConfig>();
            const auto& wcSnd = sc.get("wave_clear");
            AudioSystem::playSfx(wcSnd.path, wcSnd.volume);
            WaveSystem::startNextWave(em);
            TracyMessageL("LadderAscend");
            return;
        }
    }
}
