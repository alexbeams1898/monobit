#include "Glimmer.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

namespace glimmer
{
namespace
{
constexpr int kGlowSize = 64;         // glimmer.png is 64x64
constexpr float kFadeSpeed = 6.0f;    // alpha lerp rate toward target (per second)
constexpr float kBrightMax = 0.55f;   // peak glow alpha when faced (unobserved)
constexpr float kPulseAmp = 0.12f;    // breathing depth, as a fraction of base alpha
constexpr float kPulseHz = 0.7f;      // breathing rate
constexpr float kObservedDim = 0.18f; // muted persistent glow once observed (stale/used)
constexpr float kFacedBoost = 1.6f;   // observed glow is this much brighter when faced

// Warm "notice me" tint for an unobserved observable.
constexpr float kWarmR = 1.0f;
constexpr float kWarmG = 0.94f;
constexpr float kWarmB = 0.78f;

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
        reg.emplace<TintOverride>(e, TintOverride{kWarmR, kWarmG, kWarmB});
    }
}

void update(EntityManager& em, const observations::State& obs, const growth::GrowthState& growth,
            float px, float py, float dir_x, float dir_y, float dt)
{
    sPhase += dt * kPulseHz * 6.2831853f;
    const float breath = std::sin(sPhase);                // -1..1 breathing modulation
    const float lerp = 1.0f - std::exp(-kFadeSpeed * dt); // framerate-independent

    const std::string faced = observations::facedId(obs, growth, px, py, dir_x, dir_y);

    for (auto [e, glim, spr, tint] : em.registry().view<Glimmer, Sprite, TintOverride>().each())
    {
        const std::string& id = glim.observable_id;
        const bool isFaced = id == faced;
        const observations::Signal sig = observations::signalFor(obs, growth, id);

        // The STEADY target for this state -- an UNOBSERVED spot glows warm when
        // faced ("come look"); once OBSERVED it keeps a muted glow (stale/used), a
        // touch brighter when faced. Thoughts are NOT signposted.
        const bool unobserved = sig == observations::Signal::Unobserved;
        float target = kObservedDim; // observed: muted persistent glow
        if (unobserved)
            target = isFaced ? kBrightMax : 0.0f;
        else if (isFaced)
            target = kObservedDim * kFacedBoost;

        // Lerp the SMOOTHED BASE toward the steady target (a clean fade in/out),
        // then display base + breathing on top -- scaled by how faded-in the glow
        // is, so a first appearance ramps 0->base cleanly and never flashes.
        glim.base_alpha += (target - glim.base_alpha) * lerp;
        spr.alpha = glim.base_alpha + kPulseAmp * breath * glim.base_alpha;

        tint.r += (kWarmR - tint.r) * lerp;
        tint.g += (kWarmG - tint.g) * lerp;
        tint.b += (kWarmB - tint.b) * lerp;
    }
}
} // namespace glimmer
