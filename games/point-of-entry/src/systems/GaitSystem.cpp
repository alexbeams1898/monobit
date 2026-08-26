#include "systems/GaitSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/FeelConfig.h"
#include "ops/NavUtils.h"
#include "ops/SoundOps.h"

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

// Held poses per step. Fewer is snappier cutout, more is smoother; 4 keeps the paper feel
// without the strobe of 3 at walking cadence.

} // namespace

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    // CARRYING A GAIT IS WHAT MAKES A THING A WALKER. Membership is the component, never a
    // guess from what else an entity happens to have: everything the game draws moves and has a
    // sprite -- droplets of spray, a thrown area, a pest -- and none of those walk. Given a
    // gait by whoever builds a body with legs.
    for (auto [entity, sprite, transform, prev, gait] :
         reg.view<Sprite, Transform, PreviousTransform, Gait>().each())
    {
        const float moved = std::sqrt((transform.x - prev.x) * (transform.x - prev.x) +
                                      (transform.y - prev.y) * (transform.y - prev.y));

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

        // Bracing TIGHTENS the walk rather than stopping it. He still waddles -- taking the
        // sway away outright reads as the animation breaking rather than as a man setting
        // himself -- it just travels less far, and the hop settles with it.
        const float amp = 1.0f - gait.rest;
        const float loose = 1.0f - gait.braced;

        // Quantize distance into held poses -- the walk advances in snaps, not a glide.
        const float posesPerPx = feel::current().walk.poses_per_step / feel::current().walk.stride;
        const float s =
            std::floor(gait.travelled * posesPerPx) / (feel::current().walk.poses_per_step);

        // A FOOT LANDS on every whole step. Reported from here because this is where the walk
        // IS: anything else would have to work the same number out again from speed and time,
        // and the two would part company the first time he was slowed.
        if (const int footfall = static_cast<int>(gait.travelled / feel::current().walk.stride);
            footfall != gait.footfalls)
        {
            gait.footfalls = footfall;
            if (gait.rest < 1.0f)
                sound::play("footstep");
        }

        // One hop per step, and the lean ALTERNATES by step parity -- left step, right step. Both
        // peak mid-step together: up-and-tilted is one pose, level at each footfall.
        const float hop = std::abs(std::sin(s * geom::kPi)) * feel::current().walk.hop * amp *
                          (0.6f + 0.4f * loose);
        const float side = static_cast<int>(s) % 2 == 0 ? 1.0f : -1.0f;
        const float theta = std::abs(std::sin(s * geom::kPi)) * feel::current().walk.tilt * side *
                            amp * (0.45f + 0.55f * loose);

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
