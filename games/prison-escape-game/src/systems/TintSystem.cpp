#include "systems/TintSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"

#include <tracy/Tracy.hpp>

#include <algorithm>

// Health redness kicks in below this HP fraction.
static constexpr float HP_RED_THRESHOLD = 0.6f;
// Stamina fraction below which the heartbeat fires.
static constexpr float STAMINA_HEARTBEAT_THRESHOLD = 0.4f;
// Heartbeat interval range in seconds (shrinks as stamina approaches 0).
static constexpr float HEARTBEAT_INTERVAL_HIGH = 1.4f;
static constexpr float HEARTBEAT_INTERVAL_LOW = 0.7f;

// Apply progressive red tint if HP is below threshold. Returns true if applied.
static bool tryApplyHealthTint(entt::registry& reg, entt::entity entity)
{
    const auto* hp = reg.try_get<Health>(entity);
    if (!hp || hp->max <= 0)
        return false;
    const float hp_frac = static_cast<float>(hp->current) / static_cast<float>(hp->max);
    if (hp_frac >= HP_RED_THRESHOLD)
        return false;
    const float intensity = 1.0f - hp_frac / HP_RED_THRESHOLD;
    const float gb = 1.0f - intensity * 0.85f;
    reg.emplace<TintOverride>(entity, TintOverride{1.2f, gb, gb});
    return true;
}

void TintSystem::update(EntityManager& em, double dt)
{
    ZoneScopedN("TintSystem");
    auto& reg = em.registry();
    const SoundConfig& snd = reg.ctx().get<SoundConfig>();

    // TintSystem owns TintOverride entirely. Clear the pool each frame and
    // re-apply based on current state. This guarantees no stale overrides.
    reg.clear<TintOverride>();

    // --- Damage flash: white -- highest priority, applies to ALL entities ---
    for (auto [entity, df] : reg.view<DamageFeedback>().each())
        reg.emplace<TintOverride>(entity, TintOverride{10.0f, 10.0f, 10.0f});

    // --- Player-specific tints (entities with Experience) ---
    for (auto [entity, exp] : reg.view<Experience>().each())
    {
        if (reg.all_of<TintOverride>(entity))
            continue;

        // Stat points waiting to allocate: gold.
        if (exp.stat_points > 0)
        {
            reg.emplace<TintOverride>(entity, TintOverride{1.0f, 0.9f, 0.0f});
            continue;
        }

        if (tryApplyHealthTint(reg, entity))
            continue;

        // Low stamina: desaturate toward grey below 40%.
        if (reg.all_of<Stamina>(entity))
        {
            const auto& sta = reg.get<Stamina>(entity);
            const float sta_frac = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 1.0f;
            if (sta_frac < STAMINA_HEARTBEAT_THRESHOLD)
            {
                const float t = 1.0f - sta_frac / STAMINA_HEARTBEAT_THRESHOLD;
                const float grey = 1.0f - t * 0.3f;
                reg.emplace<TintOverride>(entity, TintOverride{grey, grey, grey});
            }
        }
    }

    // --- Enemy low-health redness ---
    for (auto [entity, ai, hp] : reg.view<AIController, Health>().each())
    {
        if (reg.all_of<TintOverride>(entity))
            continue;
        tryApplyHealthTint(reg, entity);
    }

    // --- Heartbeat: fires periodically when player stamina is low ---
    static float heartbeatTimer = 0.0f;
    heartbeatTimer -= static_cast<float>(dt);

    for (auto [entity, actions, sta] : reg.view<PlayerActions, Stamina>().each())
    {
        const float frac = sta.max_stamina > 0.0f ? sta.current / sta.max_stamina : 1.0f;
        if (frac < STAMINA_HEARTBEAT_THRESHOLD && heartbeatTimer <= 0.0f)
        {
            const float t = 1.0f - frac / STAMINA_HEARTBEAT_THRESHOLD;
            const float interval =
                HEARTBEAT_INTERVAL_HIGH + t * (HEARTBEAT_INTERVAL_LOW - HEARTBEAT_INTERVAL_HIGH);
            const auto& hb = snd.get("low_stamina_heartbeat");
            AudioSystem::playSfx(hb.path, hb.volume);
            heartbeatTimer = interval;
        }
        break;
    }
}
