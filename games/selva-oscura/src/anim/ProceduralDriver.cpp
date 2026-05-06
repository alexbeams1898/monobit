#include "Tunables.h"
#include "anim/AnimationDriver.h"

#include <cmath>
#include <glm/gtc/constants.hpp>

// ---------------------------------------------------------------------------
// Procedural animation driver — implements anim::evaluate by computing
// transform offsets directly from (state, phase, params). No skeletal data,
// no clip sampling — just math curves per state. This is the implementation
// today; a future skeletal driver will implement the same evaluate() with
// ozz-animation under the hood.
//
// Curve shapes (per state) are documented inline so feel-tuning has one
// place to look. Numeric values come from selva::tuning::current() — edited
// live via the in-game ImGui panel, persisted to config/tunables.json.
// ---------------------------------------------------------------------------

namespace selva::anim
{

namespace
{

// ---- Curve helpers. Pure functions, all in [0, 1] -> [0, 1] domain unless
// noted. Trivially testable; if any of these end up reused across states
// they'll move to a shared anim_curves.h.

// Gravity arc — projectile motion under constant gravity, normalized so
// y=0 at t=0 and t=1, peak y=1.0 at t=0.5. This is mathematically the
// position function of an object launched with the right initial velocity
// to land exactly at t=1: y(t) = v₀·t - ½g·t² which solves to 4t(1-t)
// when peak=1.0 at t=0.5. Symmetric in time, but the motion *reads* as
// gravity because the velocity profile matches free-fall — slow at apex,
// fast at touchdown. Used for the hop, where "looks physical" matters.
//
// Single parameter (t) on purpose: any "peak phase" knob would produce
// unphysical motion that fights what the eye expects. Use the surrounding
// [start, end] window to control airborne duration; the arc shape is
// fixed by physics.
float gravityArc(float t)
{
    return 4.0f * t * (1.0f - t);
}

// Asymmetric parabolic arc with a movable peak — value is 0 at t=0 and t=1,
// reaches 1.0 at t=peak. Two parabolas glued at the peak. Used for posture
// curves like the lean, where the peak doesn't have to coincide with the
// temporal midpoint (a character can lean fast and unfold slow, or vice
// versa). Out-of-range peaks (<= 0 or >= 1) degrade to 0.
float parabolicArc(float t, float peak)
{
    if (peak <= 0.0f || peak >= 1.0f)
        return 0.0f;
    if (t <= peak)
    {
        const float u = t / peak;
        return 1.0f - (u - 1.0f) * (u - 1.0f);
    }
    const float u = (t - peak) / (1.0f - peak);
    return 1.0f - u * u;
}

// Smooth ease-in-out — 0 at t=0, 1 at t=1, slow start, accelerated middle,
// slow end. Shaped by `power`: 1.0 = linear, 2.0 = standard cubic-style
// S-curve, 3.0 = more aggressive. Used for tumble and translation curves
// during the roll: most of the action happens during the airborne window,
// matching the Souls feel where the character lifts, tumbles briskly,
// then settles into recovery.
float easeInOutPow(float t, float power)
{
    if (power <= 1.0f)
        return t;
    if (t < 0.5f)
        return 0.5f * std::pow(2.0f * t, power);
    return 1.0f - 0.5f * std::pow(2.0f * (1.0f - t), power);
}

// Map an overall phase t into a sub-curve's local [0, 1] window. Returns:
//   0 if t < start (the sub-curve hasn't started yet)
//   1 if t > end  (the sub-curve has completed; caller decides what that
//                 means — for monotonic curves it's "fully applied," for
//                 return-to-zero arcs the caller separately checks for
//                 out-of-window and zeros the contribution)
//   (t - start) / (end - start) otherwise
//
// This is how we layer hop / tumble / translation in independent time
// windows within the overall roll phase. Each sub-curve doesn't know
// (or care) about the others — it just runs over its own window.
float subPhase(float t, float start, float end)
{
    if (end <= start)
        return t >= end ? 1.0f : 0.0f;
    if (t <= start)
        return 0.0f;
    if (t >= end)
        return 1.0f;
    return (t - start) / (end - start);
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
        // Three sub-curves, each on its own [start, end] window inside
        // the overall roll phase. Defaults give the Souls read:
        //   * Hop window [0.0, 0.55]: character lifts, peaks early-mid,
        //     lands by phase 0.55. After landing, no more vertical motion.
        //   * Tumble window [0.15, 0.85]: rotation kicks in slightly into
        //     the hop, runs eased through the airborne moment, settles
        //     before recovery. Outside the window it contributes 0.
        //   * Translation window [0.0, 0.85]: forward motion overlaps
        //     both, eased so most of the distance lands during the
        //     airborne portion.
        // The X/Z translation is *cumulative* — driver outputs absolute
        // offset from dodge-start, caller does pos = dodge_start_pos +
        // driver.translation each frame instead of integrating.
        const auto& tun = selva::tuning::current();

        // Hop: zero outside its window, gravity arc within it. Peak is
        // fixed at the midpoint of the window because that's where real
        // gravity puts the apex; the surrounding [start, end] window
        // controls airborne duration.
        float y_offset = 0.0f;
        if (t > tun.roll_hop_start && t < tun.roll_hop_end)
        {
            const float local = subPhase(t, tun.roll_hop_start, tun.roll_hop_end);
            y_offset = gravityArc(local) * tun.roll_hop_height;
        }

        // Tumble: zero before window, hold at full revolutions after
        // (character stays oriented at landing rotation). Eased within.
        const float tumble_local = subPhase(t, tun.roll_tumble_start, tun.roll_tumble_end);
        const float tumble_eased = easeInOutPow(tumble_local, tun.roll_tumble_ease);
        const float tumble_pitch = -tun.roll_tumble_revs * glm::two_pi<float>() * tumble_eased;

        // Pre-tumble lean: a parabolic forward-pitch posture that rises and
        // unfolds within its own window. Composes additively with the
        // tumble so the character starts tipping forward before the full
        // rotation kicks in (and unfolds back to upright before landing if
        // the tumble is short / windowed earlier than the lean ends).
        // Negative pitch_offset = top tilts forward (same convention as
        // tumble). Within-window is the parabolic arc; outside-window is 0.
        float lean_pitch = 0.0f;
        if (t > tun.roll_lean_start && t < tun.roll_lean_end)
        {
            const float lean_local = subPhase(t, tun.roll_lean_start, tun.roll_lean_end);
            const float lean_arc = parabolicArc(lean_local, tun.roll_lean_peak_phase);
            lean_pitch = -tun.roll_lean_angle * lean_arc;
        }

        out.pitch_offset = tumble_pitch + lean_pitch;

        // Translation: zero before window, hold at full distance after
        // (character has reached the landing point). Eased within.
        const float trans_local = subPhase(t, tun.roll_translation_start, tun.roll_translation_end);
        const float trans_eased = easeInOutPow(trans_local, tun.roll_tumble_ease);
        const float forward_offset = trans_eased * tun.roll_distance;
        out.translation = input.params.dodge_dir * forward_offset;
        out.translation.y = y_offset;
        break;
    }

    case AnimState::DodgeBackstep:
    {
        // Translation: ease-out along dodge_dir (typically -facing). No
        // hop, no tumble — a quick hop, not a roll. Backstep keeps the
        // pure ease-out (push-off, decel) since it's a single beat with
        // no airborne phase to time around.
        const auto& tun = selva::tuning::current();
        const float u = 1.0f - t;
        const float forward_offset = (1.0f - u * u) * tun.backstep_distance;
        out.translation = input.params.dodge_dir * forward_offset;
        break;
    }
    }

    return out;
}

} // namespace selva::anim
