#include "anim/AnimationDriver.h"

#include <glm/gtc/constants.hpp>

// ---------------------------------------------------------------------------
// Procedural animation driver — implements anim::evaluate by computing
// transform offsets directly from (state, phase, params). No skeletal data,
// no clip sampling — just math curves per state. This is the implementation
// today; a future skeletal driver will implement the same evaluate() with
// ozz-animation under the hood.
//
// Curve shapes (per state) are documented inline so feel-tuning has one
// place to look.
// ---------------------------------------------------------------------------

namespace selva::anim
{

namespace
{

// ---- Tunables shared across procedural states. Constexpr so the compiler
// folds them at the call sites. Move to JSON config when there's reason to
// hot-reload, not before.

// Dodge — roll. Distance and tumble revolutions are *integrated* over the
// roll's lifetime; gameplay code owns the duration and feeds phase=0..1.
constexpr float kRollDistance = 3.5f;   // total world units traveled
constexpr float kRollHopHeight = 0.45f; // peak Y offset at phase=0.5
constexpr float kRollTumbleRevs = 1.0f; // full pitch revolutions over the roll

// Dodge — backstep. Shorter, no hop, no tumble.
constexpr float kBackstepDistance = 2.0f;

// ---- Curve helpers. Pure functions, all in [0, 1] -> [0, 1] domain unless
// noted. Trivially testable; if any of these end up reused across states
// they'll move to a shared anim_curves.h.

// Parabolic arc that's 0 at phase=0 and 1, peaks at 1.0 at phase=0.5.
// 4 * t * (1 - t) is the canonical bell shape; multiply by peak height
// at the call site.
float hopArc(float t)
{
    return 4.0f * t * (1.0f - t);
}

// Ease-out cumulative distance: 0 at t=0, 1 at t=1, fast acceleration at
// the start, decelerating to a stop. Shape is 1 - (1-t)^2. Multiply by
// total distance to get current cumulative travel.
//
// Why ease-out for a roll: matches Souls feel — push off hard at the
// start, decelerate into the recovery. Linear translation reads as
// sliding, not rolling.
float easeOutDistance(float t)
{
    const float u = 1.0f - t;
    return 1.0f - u * u;
}

} // namespace

AnimDriverOutput evaluate(const AnimDriverInput& input)
{
    AnimDriverOutput out;

    // Clamp phase defensively — gameplay code should already feed [0, 1]
    // but we don't want a NaN from a bad input to leak into a transform.
    const float t = input.phase < 0.0f ? 0.0f : (input.phase > 1.0f ? 1.0f : input.phase);

    switch (input.state)
    {
    case AnimState::None:
    case AnimState::Locomotion:
    case AnimState::DodgeRecover:
        // All neutral today. Identity offsets — caller's base transform
        // is unchanged. These cases exist so future animations have a
        // home without an API change.
        break;

    case AnimState::DodgeRoll:
    {
        // Translation: ease-out cumulative distance along dodge_dir,
        // plus parabolic hop on Y. The X/Z component is *cumulative*
        // (driver outputs absolute offset from dodge-start, not per-frame
        // velocity), so the caller computes
        //   pos = dodge_start_pos + driver.translation
        // each frame rather than integrating.
        const float forward_offset = easeOutDistance(t) * kRollDistance;
        const float y_offset = hopArc(t) * kRollHopHeight;
        out.translation = input.params.dodge_dir * forward_offset;
        out.translation.y = y_offset;

        // Visual tumble: rotate around local +X (right axis, after the
        // entity's yaw is applied). Negative angle so the cube tips
        // *forward* — top tilting toward the direction of motion.
        // Linear in phase; ease curves on rotation read as wobble, not
        // tumble.
        out.pitch_offset = -kRollTumbleRevs * glm::two_pi<float>() * t;
        break;
    }

    case AnimState::DodgeBackstep:
    {
        // Translation: ease-out along dodge_dir (typically -facing). No
        // hop, no tumble — a quick hop, not a roll.
        const float forward_offset = easeOutDistance(t) * kBackstepDistance;
        out.translation = input.params.dodge_dir * forward_offset;
        break;
    }
    }

    return out;
}

} // namespace selva::anim
