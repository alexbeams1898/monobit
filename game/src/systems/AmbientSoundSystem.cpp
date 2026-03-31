#include "systems/AmbientSoundSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "systems/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <tracy/Tracy.hpp>

static std::mt19937& rng()
{
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

static float randomRange(float lo, float hi)
{
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng());
}

void AmbientSoundSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("AmbientSoundSystem");
    const float fdt = static_cast<float>(dt);

    // Fetch player position once.
    float px = 0.0f;
    float py = 0.0f;
    bool playerFound = false;
    for (auto e : em.registry().view<PlayerActions>())
    {
        if (em.registry().all_of<Transform>(e))
        {
            const auto& t = em.registry().get<Transform>(e);
            px = t.x;
            py = t.y;
            playerFound = true;
        }
        break;
    }

    for (auto [entity, amb] : em.registry().view<AmbientSound>().each())
    {
        if (amb.paths.empty())
            continue;

        amb.timer -= fdt;
        if (amb.timer > 0.0f)
            continue;

        // Distance attenuation.
        float volFactor = 1.0f;
        if (playerFound && em.registry().all_of<Transform>(entity) && amb.max_distance > 0.0f)
        {
            const auto& t = em.registry().get<Transform>(entity);
            const float dx = t.x - px;
            const float dy = t.y - py;
            const float dist = std::sqrt(dx * dx + dy * dy);
            volFactor = std::max(0.0f, 1.0f - dist / amb.max_distance);
        }

        if (volFactor > 0.0f)
        {
            // Shuffle: cycle through all sounds before repeating.
            if (amb.shuffle_index >= static_cast<int>(amb.shuffle_order.size()))
            {
                amb.shuffle_order.resize(amb.paths.size());
                for (int i = 0; i < static_cast<int>(amb.paths.size()); ++i)
                    amb.shuffle_order[static_cast<size_t>(i)] = i;
                std::shuffle(amb.shuffle_order.begin(), amb.shuffle_order.end(), rng());
                amb.shuffle_index = 0;
            }

            const int idx = amb.shuffle_order[static_cast<size_t>(amb.shuffle_index)];
            ++amb.shuffle_index;

            const float pitch = randomRange(amb.min_pitch, amb.max_pitch);
            AudioSystem::playSfx(amb.paths[static_cast<size_t>(idx)], amb.volume * volFactor,
                                 pitch);
        }

        amb.timer = randomRange(amb.min_interval, amb.max_interval);
    }
}
