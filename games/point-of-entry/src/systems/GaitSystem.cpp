#include "systems/GaitSystem.h"

#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/FeelConfig.h"
#include "ecs/GameComponents.h"
#include "ops/NavUtils.h"
#include "ops/SoundOps.h"

#include <cmath>

#include <entt/entt.hpp>

namespace walk_bob
{
// The body is one rigid piece that hops and rocks, pivoting at its FEET, alternating side each
// step. Quantized into a few held poses per step rather than a smooth curve: the snapping is the
// intended read, not something to smooth out.

void update(EntityManager& em, float dt)
{
    auto& reg = em.registry();
    // Carrying a Gait is what makes a thing a walker. Everything drawn has a sprite and moves
    // -- spray, thrown areas, pests -- so membership has to be declared, never inferred.
    for (auto [entity, sprite, transform, prev, gait] :
         reg.view<Sprite, Transform, PreviousTransform, Gait>().each())
    {
        const float moved = std::sqrt((transform.x - prev.x) * (transform.x - prev.x) +
                                      (transform.y - prev.y) * (transform.y - prev.y));

        gait.travelled += moved;

        // Settle to rest rather than freezing mid-hop, which leaves him stopped in mid-air.
        if (moved < 0.01f)
        {
            gait.rest = std::min(1.0f, gait.rest + dt * 8.0f);
            if (gait.rest >= 1.0f)
                gait.travelled = 0.0f;
        }
        else
            gait.rest = 0.0f;

        // Bracing tightens the walk rather than stopping it: removing the sway outright reads
        // as the animation breaking, not as a man setting himself.
        const float amp = 1.0f - gait.rest;
        const float loose = 1.0f - gait.braced;

        // Quantize distance into held poses -- the walk advances in snaps, not a glide.
        const float posesPerPx = feel::current().walk.poses_per_step / feel::current().walk.stride;
        const float s =
            std::floor(gait.travelled * posesPerPx) / (feel::current().walk.poses_per_step);

        // Footfalls are reported from here because this is where the step count exists.
        // Deriving it again from speed and time gives two answers the moment he is slowed.
        if (const int footfall = static_cast<int>(gait.travelled / feel::current().walk.stride);
            footfall != gait.footfalls)
        {
            gait.footfalls = footfall;
            if (gait.rest < 1.0f)
                sound::play("footstep");
        }

        // Hop and lean peak together mid-step, so up-and-tilted is one pose and each footfall
        // lands level. The lean alternates on step parity.
        const float hop = std::abs(std::sin(s * geom::kPi)) * feel::current().walk.hop * amp *
                          (0.6f + 0.4f * loose);
        const float side = static_cast<int>(s) % 2 == 0 ? 1.0f : -1.0f;
        const float theta = std::abs(std::sin(s * geom::kPi)) * feel::current().walk.tilt * side *
                            amp * (0.45f + 0.55f * loose);

        // The engine rotates a sprite about its centre, which would swing the feet out from
        // under him. Offsetting by the base's displacement puts the hinge on the ground.
        const float halfH = static_cast<float>(sprite.src_h) * 0.5f;
        sprite.rotation = theta;
        sprite.draw_offset_x = std::sin(theta) * halfH;
        sprite.draw_offset_y = -hop + (1.0f - std::cos(theta)) * halfH;
    }
}

} // namespace walk_bob
