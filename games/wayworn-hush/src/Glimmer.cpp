#include "Glimmer.h"

#include "JsonConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace glimmer
{
namespace
{
float sPhase = 0.0f; // shared breathing phase (advanced in update)
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
        const entt::entity e = reg.create();
        reg.emplace<Transform>(e, Transform{o.x, o.y});
        reg.emplace<Glimmer>(e, Glimmer{o.id});

        Sprite spr{};
        spr.texture_path = cfg.sprite;
        spr.src_w = cfg.size;
        spr.src_h = cfg.size;
        spr.layer = 3;    // above characters/props (layer 2) so the "come look" shimmer is
                          // never hidden by the very object it marks (a tree over its own glow)
        spr.alpha = 0.0f; // starts invisible; driven by update()
        reg.emplace<Sprite>(e, spr);
        // Warm tint MULTIPLIED into the glow texture (TintOverride keeps the
        // PNG's soft radial alpha; SolidColor would replace it with a flat fill).
        reg.emplace<TintOverride>(e, TintOverride{cfg.warm_r, cfg.warm_g, cfg.warm_b});
    }
}

void update(EntityManager& em, const observations::State& obs, const growth::GrowthState& growth,
            const Config& cfg, float px, float py, float dt)
{
    sPhase += dt * cfg.pulse_hz * 6.2831853f;
    const float breath = std::sin(sPhase);                    // -1..1 breathing modulation
    const float lerp = 1.0f - std::exp(-cfg.fade_speed * dt); // framerate-independent

    const std::string faced = observations::facedId(obs, growth, px, py);

    for (auto [e, glim, spr, tint] : em.registry().view<Glimmer, Sprite, TintOverride>().each())
    {
        const std::string& id = glim.observable_id;
        const bool isFaced = id == faced;
        const observations::Signal sig = observations::signalFor(obs, growth, id);

        // The STEADY target for this state. An UNOBSERVED spot glows warm when you're within
        // interact_reach of its box (lights up as you walk up, like a Souls prompt); OFF
        // beyond. Once OBSERVED it keeps a muted glow (stale/used), a touch brighter while
        // you're near it. Thoughts are NOT signposted.
        const bool unobserved = sig == observations::Signal::Unobserved;
        float target = cfg.observed_dim; // observed: muted persistent glow
        if (unobserved)
            target = cfg.bright_max * observations::glowStrength(obs, growth, id, px, py);
        else if (isFaced)
            target = cfg.observed_dim * cfg.faced_boost;

        // Lerp the SMOOTHED BASE toward the steady target (a clean fade in/out),
        // then display base + breathing on top -- scaled by how faded-in the glow
        // is, so a first appearance ramps 0->base cleanly and never flashes.
        glim.base_alpha += (target - glim.base_alpha) * lerp;
        spr.alpha = glim.base_alpha + cfg.pulse_amp * breath * glim.base_alpha;

        tint.r += (cfg.warm_r - tint.r) * lerp;
        tint.g += (cfg.warm_g - tint.g) * lerp;
        tint.b += (cfg.warm_b - tint.b) * lerp;
    }
}
} // namespace glimmer
