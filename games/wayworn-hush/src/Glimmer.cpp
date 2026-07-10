#include "Glimmer.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace glimmer
{
namespace
{
constexpr int kGlowSize = 64;       // glimmer.png is 64x64
constexpr float kFadeSpeed = 6.0f;  // alpha lerp rate toward target (per second)
constexpr float kBrightMax = 0.55f; // peak glow alpha
constexpr float kPulseAmp = 0.12f;  // gentle breathing on top of the base
constexpr float kPulseHz = 0.7f;

float sPhase = 0.0f; // shared breathing phase (advanced in update)
} // namespace

void spawn(EntityManager& em, const observations::State& obs)
{
    auto& reg = em.registry();
    for (const auto& o : obs.observables)
    {
        const entt::entity e = reg.create();
        reg.emplace<Transform>(e, Transform{o.x, o.y});
        reg.emplace<Glimmer>(e, Glimmer{o.id});

        Sprite spr{};
        spr.texture_path = "assets/sprites/glimmer.png";
        spr.src_w = kGlowSize;
        spr.src_h = kGlowSize;
        spr.layer = 1;    // above ground tiles, below characters (layer 2)
        spr.alpha = 0.0f; // starts invisible; driven by update()
        reg.emplace<Sprite>(e, spr);
        // Warm tint MULTIPLIED into the glow texture (TintOverride keeps the
        // PNG's soft radial alpha; SolidColor would replace it with a flat fill).
        reg.emplace<TintOverride>(e, TintOverride{1.0f, 0.94f, 0.78f});
    }
}

void update(EntityManager& em, const observations::State& obs, float px, float py, float dir_x,
            float dir_y, float dt)
{
    sPhase += dt * kPulseHz * 6.2831853f;
    const float pulse = kBrightMax + kPulseAmp * std::sin(sPhase);

    // The single observable the player faces this frame brightens; all others
    // fade. An exhausted (nothing-new) observable stays dim even when faced.
    const std::string faced = observations::facedId(obs, px, py, dir_x, dir_y);

    for (auto [e, glim, spr] : em.registry().view<Glimmer, Sprite>().each())
    {
        const bool active =
            glim.observable_id == faced && !observations::exhausted(obs, glim.observable_id);
        const float target = active ? pulse : 0.0f;
        // Framerate-independent lerp toward the target alpha.
        spr.alpha += (target - spr.alpha) * (1.0f - std::exp(-kFadeSpeed * dt));
    }
}
} // namespace glimmer
