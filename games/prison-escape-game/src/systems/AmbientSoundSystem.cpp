#include "systems/AmbientSoundSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "systems/AudioSystem.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <random>

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

// Distance attenuation factor (0 = silent, 1 = full volume).
static float distanceFactor(bool playerFound, float px, float py, const Transform* t,
                            float max_distance)
{
    if (!playerFound || t == nullptr || max_distance <= 0.0f)
        return 1.0f;
    const float dx = t->x - px;
    const float dy = t->y - py;
    const float dist = std::sqrt(dx * dx + dy * dy);
    return std::max(0.0f, 1.0f - dist / max_distance);
}

// Play a random sound from a shuffled pool. Returns true if a sound was played.
static bool playShuffle(const std::vector<std::string>& paths, float volume, float min_pitch,
                        float max_pitch, float volFactor, int& shuffle_index,
                        std::vector<int>& shuffle_order)
{
    if (volFactor <= 0.0f)
        return false;

    if (shuffle_index >= static_cast<int>(shuffle_order.size()))
    {
        shuffle_order.resize(paths.size());
        for (int i = 0; i < static_cast<int>(paths.size()); ++i)
            shuffle_order[static_cast<size_t>(i)] = i;
        std::shuffle(shuffle_order.begin(), shuffle_order.end(), rng());
        shuffle_index = 0;
    }

    const int idx = shuffle_order[static_cast<size_t>(shuffle_index)];
    ++shuffle_index;

    const float pitch = randomRange(min_pitch, max_pitch);
    AudioSystem::playSfx(paths[static_cast<size_t>(idx)], volume * volFactor, pitch);
    return true;
}

// Tracked variant — returns a voice index that can be stopped later.
static int playShuffleTracked(const std::vector<std::string>& paths, float volume, float min_pitch,
                              float max_pitch, float volFactor, int& shuffle_index,
                              std::vector<int>& shuffle_order)
{
    if (volFactor <= 0.0f)
        return -1;

    if (shuffle_index >= static_cast<int>(shuffle_order.size()))
    {
        shuffle_order.resize(paths.size());
        for (int i = 0; i < static_cast<int>(paths.size()); ++i)
            shuffle_order[static_cast<size_t>(i)] = i;
        std::shuffle(shuffle_order.begin(), shuffle_order.end(), rng());
        shuffle_index = 0;
    }

    const int idx = shuffle_order[static_cast<size_t>(shuffle_index)];
    ++shuffle_index;

    const float pitch = randomRange(min_pitch, max_pitch);
    return AudioSystem::playSfxTracked(paths[static_cast<size_t>(idx)], volume * volFactor, pitch);
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

    // Ambient sounds (idle / always-on).
    for (auto [entity, amb] : em.registry().view<AmbientSound>().each())
    {
        if (amb.paths.empty())
            continue;

        if (em.registry().all_of<Dead>(entity))
            continue;

        if (em.registry().all_of<DamageFeedback>(entity))
        {
            amb.timer = randomRange(amb.min_interval, amb.max_interval);
            continue;
        }

        amb.timer -= fdt;
        if (amb.timer > 0.0f)
            continue;

        const auto* t = em.registry().try_get<Transform>(entity);
        const float vol = distanceFactor(playerFound, px, py, t, amb.max_distance);
        playShuffle(amb.paths, amb.volume, amb.min_pitch, amb.max_pitch, vol, amb.shuffle_index,
                    amb.shuffle_order);
        amb.timer = randomRange(amb.min_interval, amb.max_interval);
    }

    // Aggro sounds (only while AIController state != Idle).
    for (auto [entity, aggro, ai] : em.registry().view<AggroSound, AIController>().each())
    {
        if (aggro.paths.empty())
            continue;

        // Stop and suppress on death or hit.
        if (em.registry().all_of<Dead>(entity) || em.registry().all_of<DamageFeedback>(entity))
        {
            if (aggro.voice >= 0)
            {
                AudioSystem::stopSfx(aggro.voice);
                aggro.voice = -1;
            }
            aggro.timer = randomRange(aggro.min_interval, aggro.max_interval);
            continue;
        }

        const bool isAggro = (ai.state != AIController::State::Idle);

        // Just de-aggro'd — reset for next time.
        if (!isAggro)
        {
            aggro.was_aggro = false;
            aggro.timer = 0.0f;
            continue;
        }

        // First frame of aggro — play immediately.
        if (!aggro.was_aggro)
        {
            aggro.was_aggro = true;
            aggro.timer = 0.0f;
        }

        aggro.timer -= fdt;
        if (aggro.timer > 0.0f)
            continue;

        const auto* t = em.registry().try_get<Transform>(entity);
        const float vol = distanceFactor(playerFound, px, py, t, aggro.max_distance);
        aggro.voice =
            playShuffleTracked(aggro.paths, aggro.volume, aggro.min_pitch, aggro.max_pitch, vol,
                               aggro.shuffle_index, aggro.shuffle_order);
        aggro.timer = randomRange(aggro.min_interval, aggro.max_interval);
    }
}
