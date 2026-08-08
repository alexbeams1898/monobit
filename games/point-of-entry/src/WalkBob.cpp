#include "WalkBob.h"

#include "Combat.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"

#include <cmath>

#include <entt/entt.hpp>

namespace walk_bob
{
namespace
{

// THE CUTOUT WALK. The body is one rigid piece that HOPS and ROCKS: each step it tilts to one
// side, pivoting at its FEET, while rising in a small hop -- then tilts the other way on the next
// step. Because the pivot is at the base, the head sweeps diagonally up-and-out with every step,
// which is the whole goofy signature; a body that slides sideways instead of tilting reads as
// jiggle, because nothing pivots.
//
// Deliberately QUANTIZED into a few held poses per step rather than a smooth curve -- the source
// style is jerky cutout animation, and the choppiness is the charm, not a defect to smooth out.

// World px travelled per step (one hop, one tilt). The tilt alternates each step. This is the
// CADENCE knob: at a given walk speed, a longer stride means fewer steps per second. Too short
// and the rock becomes a shudder -- 13px at 120px/s was nine steps a second, triple a natural
// rhythm, and every pose-snap landed three times as often as the eye wanted it.
constexpr float kStrideLength = 25.0f;

constexpr float kHopHeight = 2.0f;
constexpr float kTiltMax = 0.12f; // radians (~7 deg) of rock at the peak of a step

// Held poses per step. Fewer is snappier cutout, more is smoother; 4 keeps the paper feel
// without the strobe of 3 at walking cadence.
constexpr float kPosesPerStep = 4.0f;

constexpr float kPi = 3.14159265f;

} // namespace

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    for (auto [entity, sprite, transform, prev] :
         reg.view<Sprite, Transform, PreviousTransform>().each())
    {
        // Vermin do not walk, they vibrate -- and both write the same draw offset, so whichever
        // ran last would win. Creatures own their own motion; this is the two-legged gait.
        if (reg.all_of<Vermin>(entity))
            continue;

        const float moved = std::sqrt((transform.x - prev.x) * (transform.x - prev.x) +
                                      (transform.y - prev.y) * (transform.y - prev.y));

        auto& gait = reg.get_or_emplace<Gait>(entity, Gait{});
        gait.travelled += moved;

        // Settle to rest when standing still rather than freezing mid-hop, or a character stops
        // walking while suspended in the air.
        if (moved < 0.01f)
        {
            gait.rest = std::min(1.0f, gait.rest + dt * 8.0f);
            if (gait.rest >= 1.0f)
                gait.travelled = 0.0f;
        }
        else
            gait.rest = 0.0f;

        const float amp = 1.0f - gait.rest;

        // Quantize distance into held poses -- the walk advances in snaps, not a glide.
        const float posesPerPx = kPosesPerStep / kStrideLength;
        const float s = std::floor(gait.travelled * posesPerPx) / (kPosesPerStep);

        // One hop per step, and the lean ALTERNATES by step parity -- left step, right step. Both
        // peak mid-step together: up-and-tilted is one pose, level at each footfall.
        const float hop = std::abs(std::sin(s * kPi)) * kHopHeight * amp;
        const float side = static_cast<int>(s) % 2 == 0 ? 1.0f : -1.0f;
        const float theta = std::abs(std::sin(s * kPi)) * kTiltMax * side * amp;

        // Pivot at the FEET. The engine rotates a sprite about its centre, which would swing the
        // feet out from under him -- offsetting by the base's displacement puts the hinge where
        // the ground is, and the head does the travelling.
        const float halfH = static_cast<float>(sprite.src_h) * 0.5f;
        sprite.rotation = theta;
        sprite.draw_offset_x = std::sin(theta) * halfH;
        sprite.draw_offset_y = -hop + (1.0f - std::cos(theta)) * halfH;
    }
}

} // namespace walk_bob
