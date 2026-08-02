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
float sPhase = 0.0f; // shared beating phase (advanced in update)

// The sprite entity whose silhouette an encounter box should light: the drawn
// thing whose CENTER the box contains, nearest the box's own center. An encounter
// is authored ON something (a TV, a clock), so the overlap is the link -- no
// authored pairing to keep in step with the map.
//
// Containment, not mere overlap: a neighbour that merely grazes the box (the
// stand under the TV, the nightstand beside the clock) would otherwise light up
// too. Resolved at spawn, BEFORE any character exists in the level, so a body
// standing on a spot can never be mistaken for the spot's art. entt::null when
// the box covers bare ground (nothing to outline).
entt::entity artUnder(EntityManager& em, float cx, float cy, float w, float h)
{
    auto& reg = em.registry();
    entt::entity best = entt::null;
    float bestDist = 0.0f;
    for (auto [e, t, spr] : reg.view<Transform, Sprite>().each())
    {
        if (spr.src_w <= 0 || spr.src_h <= 0)
            continue;
        const float dx = t.x - cx;
        const float dy = t.y - cy;
        if (std::abs(dx) > w * 0.5f || std::abs(dy) > h * 0.5f)
            continue; // its center is outside the box -- a neighbour, not the subject
        const float d = dx * dx + dy * dy;
        if (best == entt::null || d < bestDist)
        {
            best = e;
            bestDist = d;
        }
    }
    return best;
}
} // namespace

void load(Config& cfg, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    cfg.fade_speed = j.value("fade_speed", cfg.fade_speed);
    cfg.perception_scales = j.value("perception_scales", cfg.perception_scales);
    cfg.brightness = j.value("brightness", cfg.brightness);
    cfg.pulse_amp = j.value("pulse_amp", cfg.pulse_amp);
    cfg.pulse_hz = j.value("pulse_hz", cfg.pulse_hz);
    cfg.rim_width = j.value("rim_width", cfg.rim_width);
    cfg.warm_r = j.value("warm_r", cfg.warm_r);
    cfg.warm_g = j.value("warm_g", cfg.warm_g);
    cfg.warm_b = j.value("warm_b", cfg.warm_b);
}

void spawn(EntityManager& em, const psyche::State& obs, const Config& cfg,
           const std::unordered_map<std::string, entt::entity>& bodies,
           const std::unordered_set<std::string>& gone)
{
    (void)cfg;
    auto& reg = em.registry();
    for (const auto& o : obs.encounters)
    {
        // Observe-mode only: one entity that IS both the interactable (input) and the
        // highlight state. An Enter (ambient) encounter fires on proximity, not the
        // interact verb, so it carries no highlight.
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
        // The art this box lights. A PERSON's encounter lights that person, by name:
        // a body walks around and stands in front of scenery, so looking under the
        // box would light whatever they happen to be standing on (the oven behind
        // them). Everything else adopts the prop art under its box -- props don't
        // move, and a box over bare ground simply has nothing to outline.
        const auto body = bodies.find(o.speaker);
        const bool isPerson = body != bodies.end();
        const entt::entity art = isPerson ? body->second : artUnder(em, o.x, o.y, o.w, o.h);
        reg.emplace<Glimmer>(e, Glimmer{0.0f, art, isPerson});
        // Encounter-only interactable: no direct action (a "take" deed, if any, lives in
        // the observation menu). The rim marks it; examining opens its reading.
        interaction::Interactable inter{};
        inter.w = o.w;
        inter.h = o.h;
        inter.placement_id = o.placement_id; // so consuming the spot can be remembered
        inter.observe_id = o.id;
        reg.emplace<interaction::Interactable>(e, inter);
    }
}

void refreshPresence(EntityManager& em, const psyche::State& obs, const growth::GrowthState& growth)
{
    // The single source for "does this encounter exist right now". Set BEFORE the interaction
    // resolve reads it, so a hidden encounter (visible_when unmet) is no target -- neither
    // verb can fire on it -- and a just-revealed one becomes interactable the same frame. A
    // non-encounter interactable (an item) has no observe_id and is always present.
    auto& reg = em.registry();
    for (auto [e, inter] : reg.view<interaction::Interactable>().each())
        inter.present = inter.observe_id.empty() || psyche::visible(obs, growth, inter.observe_id);
}

void update(EntityManager& em, const growth::GrowthState& growth, const formulas::Config& formulas,
            const Config& cfg, float dt)
{
    sPhase += dt * cfg.pulse_hz * 6.2831853f;
    const float breath = std::sin(sPhase);                    // -1..1 beating modulation
    const float lerp = 1.0f - std::exp(-cfg.fade_speed * dt); // framerate-independent
    // How brightly a lit spot reads. `perception_scales` hands the dial to the stat
    // (formulas::glowBrightness -- the world seen through the character's eyes);
    // off, every spot lights at `brightness`, which is what you want while the
    // highlight itself is being tuned.
    const float peak =
        cfg.perception_scales
            ? formulas::glowBrightness(formulas, growth::statLevel(growth, "perception"))
            : cfg.brightness;

    auto& reg = em.registry();
    for (auto [e, glim, inter] : reg.view<Glimmer, interaction::Interactable>().each())
    {
        // A person's encounter travels with them: the box marks where they ARE, so
        // walking up to someone who has wandered off still finds them (and one who
        // walks away takes their box along instead of leaving it behind).
        if (glim.follows_body && reg.valid(glim.art))
        {
            const auto& body = reg.get<Transform>(glim.art);
            auto& box = reg.get<Transform>(e);
            box.x = body.x;
            box.y = body.y;
        }
        // One rule: lit only while this is the active target (in reach and faced, or
        // hovered) AND present. Otherwise fade to 0 -- nothing lingers. Presence was
        // set by refreshPresence() before the interaction resolve this frame.
        const float target = (inter.active && inter.present) ? peak : 0.0f;
        glim.level += (target - glim.level) * lerp;

        if (glim.art == entt::null || !reg.valid(glim.art))
            continue;
        // Dark below a hair of brightness: carrying a rim at alpha ~0 costs a draw
        // pass per frame and can leave a ghost line on some blends.
        if (glim.level < 0.01f)
        {
            if (reg.all_of<Outline>(glim.art))
                reg.remove<Outline>(glim.art);
            continue;
        }
        // The beat rides ON the faded-in level, so a rim never flashes at full
        // strength the instant it appears.
        const float alpha = glim.level * (1.0f + cfg.pulse_amp * breath);
        reg.emplace_or_replace<Outline>(
            glim.art, Outline{cfg.warm_r, cfg.warm_g, cfg.warm_b, cfg.rim_width, alpha});
    }
}
} // namespace glimmer
