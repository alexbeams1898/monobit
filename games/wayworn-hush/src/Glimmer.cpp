#include "Glimmer.h"

#include "Interaction.h"
#include "JsonConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace glimmer
{
namespace
{
float sPhase = 0.0f; // shared breathing phase (advanced in update)

// The glow sprite for an observation: a soft radial PNG whose alpha is driven by update()
// and whose RGB is tinted per-entity (TintOverride multiplies the warm color in -- a
// SolidColor would flatten the radial falloff).
void attachGlowSprite(EntityManager& em, entt::entity e, const Config& cfg)
{
    auto& reg = em.registry();
    Sprite spr{};
    spr.texture_path = cfg.sprite;
    spr.src_w = cfg.size;
    spr.src_h = cfg.size;
    spr.layer = 3;    // above characters/props (layer 2) so the "come look" shimmer is
                      // never hidden by the very object it marks (a tree over its own glow)
    spr.alpha = 0.0f; // starts invisible; driven by update()
    reg.emplace<Sprite>(e, spr);
    reg.emplace<TintOverride>(e, TintOverride{cfg.warm_r, cfg.warm_g, cfg.warm_b});
}
} // namespace

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.sprite = j.value("sprite", cfg.sprite);
    cfg.size = j.value("size", cfg.size);
    cfg.fade_speed = j.value("fade_speed", cfg.fade_speed);
    cfg.bright_max = j.value("bright_max", cfg.bright_max);
    cfg.pulse_amp = j.value("pulse_amp", cfg.pulse_amp);
    cfg.pulse_hz = j.value("pulse_hz", cfg.pulse_hz);
    cfg.observed_dim = j.value("observed_dim", cfg.observed_dim);
    cfg.faced_boost = j.value("faced_boost", cfg.faced_boost);
    cfg.warm_r = j.value("warm_r", cfg.warm_r);
    cfg.warm_g = j.value("warm_g", cfg.warm_g);
    cfg.warm_b = j.value("warm_b", cfg.warm_b);
}

void spawn(EntityManager& em, const observations::State& obs, const Config& cfg)
{
    auto& reg = em.registry();
    for (const auto& o : obs.observables)
    {
        // Observe-mode only: one entity that IS both the interactable (input) and its
        // glimmer (highlight). An Enter (ambient) observable fires on proximity, not the
        // interact verb, so it carries no glimmer. Warm tint; the observed/unobserved
        // dimming is refreshed each frame by setObservationTargets.
        if (o.trigger != observations::Trigger::Observe)
            continue;
        const entt::entity e = reg.create();
        reg.emplace<Transform>(e, Transform{o.x, o.y});
        reg.emplace<Glimmer>(e, Glimmer{cfg.bright_max, 0.0f, cfg.warm_r, cfg.warm_g, cfg.warm_b});
        // Observable-only interactable: no direct action (a "take" deed, if any, lives in
        // the observation menu). The glimmer marks it; examining opens its reading.
        interaction::Interactable inter{};
        inter.w = o.w;
        inter.h = o.h;
        inter.observe_id = o.id;
        reg.emplace<interaction::Interactable>(e, inter);
        attachGlowSprite(em, e, cfg);
    }
}

void setObservationTargets(EntityManager& em, const observations::State& obs,
                           const growth::GrowthState& growth, const Config& cfg)
{
    auto& reg = em.registry();
    // Only observable interactables carry an observation signal; the id lives in the
    // Interactable, so the glimmer stays id-free (kind-agnostic component).
    for (auto [e, glim, inter] : reg.view<Glimmer, interaction::Interactable>().each())
    {
        if (inter.observe_id.empty())
            continue;
        const observations::Signal sig = observations::signalFor(obs, growth, inter.observe_id);
        // Unobserved: dark until it's the active target (a Souls-style "notice me" prompt).
        // Observed: a muted persistent glow (stale/used), a touch brighter while active.
        if (sig == observations::Signal::Unobserved)
        {
            glim.idle_alpha = 0.0f;
            glim.active_alpha = cfg.bright_max;
        }
        else
        {
            glim.idle_alpha = cfg.observed_dim;
            glim.active_alpha = cfg.observed_dim * cfg.faced_boost;
        }
    }
}

void update(EntityManager& em, const Config& cfg, float dt)
{
    sPhase += dt * cfg.pulse_hz * 6.2831853f;
    const float breath = std::sin(sPhase);                    // -1..1 breathing modulation
    const float lerp = 1.0f - std::exp(-cfg.fade_speed * dt); // framerate-independent

    auto& reg = em.registry();
    for (auto [e, glim, spr, tint] : reg.view<Glimmer, Sprite, TintOverride>().each())
    {
        // "active" = the InteractionSystem picked this interactable as the current target
        // (in reach or hovered). Its steady target is active_alpha when active, idle_alpha
        // otherwise -- both set by setObservationTargets.
        const auto* inter = reg.try_get<interaction::Interactable>(e);
        const bool active = inter && inter->active;
        const float target = active ? glim.active_alpha : glim.idle_alpha;

        // Lerp the SMOOTHED BASE toward the steady target (a clean fade in/out), then
        // display base + breathing on top -- scaled by how faded-in the glow is, so a
        // first appearance ramps 0->base cleanly and never flashes.
        glim.base_alpha += (target - glim.base_alpha) * lerp;
        spr.alpha = glim.base_alpha + cfg.pulse_amp * breath * glim.base_alpha;

        tint.r += (glim.tint_r - tint.r) * lerp;
        tint.g += (glim.tint_g - tint.g) * lerp;
        tint.b += (glim.tint_b - tint.b) * lerp;
    }
}
} // namespace glimmer
