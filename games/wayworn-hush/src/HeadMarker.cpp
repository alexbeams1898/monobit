#include "HeadMarker.h"

#include "JsonConfig.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace head_marker
{
namespace
{
float sPhase = 0.0f; // shared breathing phase (advanced in update)
} // namespace

void load(HeadMarkerConfig& out, const std::string& path)
{
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;
    out.sprite = j.value("sprite", out.sprite);
    out.size = j.value("size", out.size);
    out.head_offset = j.value("head_offset", out.head_offset);
    out.side_offset = j.value("side_offset", out.side_offset);
    out.peak_alpha = j.value("peak_alpha", out.peak_alpha);
    out.fade_speed = j.value("fade_speed", out.fade_speed);
    out.pulse_amp = j.value("pulse_amp", out.pulse_amp);
    out.pulse_hz = j.value("pulse_hz", out.pulse_hz);
}

void spawn(EntityManager& em, const HeadMarkerConfig& cfg)
{
    auto& reg = em.registry();
    const entt::entity e = reg.create();
    reg.emplace<Transform>(e, Transform{0.0f, 0.0f}); // synced to the player in update()

    Sprite spr{};
    spr.texture_path = cfg.sprite;
    spr.src_w = cfg.size;
    spr.src_h = cfg.size;
    spr.layer = 3;    // above characters (layer 2) so it always reads over the head
    spr.alpha = 0.0f; // hidden until a thought pops it
    reg.emplace<Sprite>(e, spr);
    // No tint: the bubble is white (its own art). Faculty is signalled by the
    // notebook entry's hue, not the popup.
    reg.emplace<HeadMarker>(e, HeadMarker{});
}

void set(EntityManager& em, bool shown)
{
    for (auto [e, hm] : em.registry().view<HeadMarker>().each())
        hm.shown = shown;
}

void update(EntityManager& em, const HeadMarkerConfig& cfg, float px, float py, float dt)
{
    sPhase += dt * cfg.pulse_hz * 6.2831853f;
    const float breath = std::sin(sPhase);
    const float lerp = 1.0f - std::exp(-cfg.fade_speed * dt); // framerate-independent

    for (auto [e, hm, t, spr] : em.registry().view<HeadMarker, Transform, Sprite>().each())
    {
        // The stem base should touch the head; the cloud floats above it. The engine
        // draws the sprite CENTERED on the Transform, and the art is authored bottom-
        // up (stem at the bottom edge), so place the center half a sprite-height above
        // the head point -- putting the sprite's bottom edge (the stem base) at the head.
        const float headY = py - cfg.head_offset;
        t.x = px + cfg.side_offset;
        t.y = headY - static_cast<float>(spr.src_h) * 0.5f;

        // Fade the smoothed base toward the target, breathing on top -- scaled by
        // the fade level so a first appearance ramps 0->peak cleanly (no flash),
        // mirroring the glimmer pattern.
        const float target = hm.shown ? cfg.peak_alpha : 0.0f;
        hm.base_alpha += (target - hm.base_alpha) * lerp;
        spr.alpha = hm.base_alpha + cfg.pulse_amp * breath * hm.base_alpha;
    }
}
} // namespace head_marker
