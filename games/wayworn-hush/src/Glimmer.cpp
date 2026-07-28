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
    cfg.pulse_amp = j.value("pulse_amp", cfg.pulse_amp);
    cfg.pulse_hz = j.value("pulse_hz", cfg.pulse_hz);
    cfg.warm_r = j.value("warm_r", cfg.warm_r);
    cfg.warm_g = j.value("warm_g", cfg.warm_g);
    cfg.warm_b = j.value("warm_b", cfg.warm_b);
}

void spawn(EntityManager& em, const psyche::State& obs, const Config& cfg,
           const std::unordered_set<std::string>& gone)
{
    auto& reg = em.registry();
    for (const auto& o : obs.encounters)
    {
        // Observe-mode only: one entity that IS both the interactable (input) and its
        // glimmer (highlight). An Enter (ambient) encounter fires on proximity, not the
        // interact verb, so it carries no glimmer.
        if (o.trigger != psyche::Trigger::Observe)
            continue;
        // Not placed in the current level -> not here (content lives elsewhere in the world).
        if (o.placement_id.empty())
            continue;
        // A spot this pilgrim consumed is not here any more, whatever the map says.
        if (gone.count(o.placement_id) > 0)
            continue;
        const entt::entity e = reg.create();
        reg.emplace<Transform>(e, Transform{o.x, o.y});
        reg.emplace<Glimmer>(e, Glimmer{cfg.warm_r, cfg.warm_g, cfg.warm_b, 0.0f});
        // Encounter-only interactable: no direct action (a "take" deed, if any, lives in
        // the observation menu). The glimmer marks it; examining opens its reading.
        interaction::Interactable inter{};
        inter.w = o.w;
        inter.h = o.h;
        inter.placement_id = o.placement_id; // so consuming the spot can be remembered
        inter.observe_id = o.id;
        reg.emplace<interaction::Interactable>(e, inter);
        attachGlowSprite(em, e, cfg);
    }
}

void refreshPresence(EntityManager& em, const psyche::State& obs,
                     const growth::GrowthState& growth)
{
    // The single source for "does this encounter exist right now". Set BEFORE the interaction
    // resolve reads it, so a hidden encounter (visible_when unmet) is no target -- neither
    // verb can fire on it -- and a just-revealed one becomes interactable the same frame. A
    // non-encounter interactable (an item) has no observe_id and is always present.
    auto& reg = em.registry();
    for (auto [e, inter] : reg.view<interaction::Interactable>().each())
        inter.present =
            inter.observe_id.empty() || psyche::visible(obs, growth, inter.observe_id);
}

void update(EntityManager& em, const growth::GrowthState& growth, const formulas::Config& formulas,
            const Config& cfg, float dt)
{
    sPhase += dt * cfg.pulse_hz * 6.2831853f;
    const float breath = std::sin(sPhase);                    // -1..1 breathing modulation
    const float lerp = 1.0f - std::exp(-cfg.fade_speed * dt); // framerate-independent
    // Peak glow = the Perception formula, the same for every spot (the stat is the dial).
    const float peak = formulas::glowBrightness(formulas, growth::statLevel(growth, "perception"));

    auto& reg = em.registry();
    for (auto [e, glim, spr, tint, inter] :
         reg.view<Glimmer, Sprite, TintOverride, interaction::Interactable>().each())
    {
        // One rule: glow only while this is the active target (in reach / hovered) AND present;
        // its peak brightness is the Perception formula. Otherwise fade to 0 -- nothing lingers.
        // Presence was set by refreshPresence() before the interaction resolve this frame.
        const float target = (inter.active && inter.present) ? peak : 0.0f;

        // Lerp the SMOOTHED BASE toward the target (a clean fade in/out), then display base +
        // breathing on top -- scaled by how faded-in the glow is, so a first appearance ramps
        // 0->base cleanly and never flashes.
        glim.base_alpha += (target - glim.base_alpha) * lerp;
        spr.alpha = glim.base_alpha + cfg.pulse_amp * breath * glim.base_alpha;

        tint.r += (glim.tint_r - tint.r) * lerp;
        tint.g += (glim.tint_g - tint.g) * lerp;
        tint.b += (glim.tint_b - tint.b) * lerp;
    }
}
} // namespace glimmer
