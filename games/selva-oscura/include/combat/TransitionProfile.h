#pragma once

#include "anim/AnimationClip.h"
#include "anim/PoseSampler.h"

namespace selva::combat
{

// One bag of decisions per "fire a one-shot" call site. The animation
// system has 6 axes of choice at every transition (dodge, attack,
// block, future jump). Hard-coding those axes inline at each call site
// makes adding/changing a transition risk regressing the others, and
// makes consistency between transitions invisible.
//
// Add a new transition by adding a new constant in profiles::; never
// copy-paste the call-site scaffolding.
struct TransitionProfile
{
    enum class SourcePrep
    {
        None,
        SnapLocoToZero,
    };

    enum class Lockout
    {
        None,
        CancelWindowClose,
        WallClockSeconds,
    };

    float blend_in_seconds = 0.20f;
    float blend_out_seconds = 0.20f;
    SourcePrep source_prep = SourcePrep::None;
    Lockout lockout = Lockout::None;
    float lockout_seconds = 0.0f;
    selva::anim::PoseSampler::BodyMask mask = selva::anim::PoseSampler::BodyMask::Full;
    bool freeze_last = false;

    // Hold the loco track stable through the one-shot's Hold +
    // BlendOut phases instead of letting the SM swap it underneath.
    // Dodges/blocks set this true: the player wasn't intending a
    // stance change, the dodge fades back into the same loco the
    // player was on. Attacks keep this false: the attack triggers
    // CombatReady, and combat-idle should be live by the time the
    // attack fades out so the reveal isn't standard_idle for a
    // frame. Default false (attack-friendly).
    bool freeze_loco_during_one_shot = false;

    // Fraction of clip duration past which gameplay considers the
    // one-shot "past commitment": movement unlocks, the next chained
    // one-shot can fire. 1.0 = no early cancel (lock for full
    // duration). Used by simple commit-then-recover actions
    // (dodges, jumps); combat attacks use a separate per-clip rhythm
    // window and keep this at 1.0. See PoseSampler::isOneShotPastCancelFraction.
    float cancel_fraction = 1.0f;
};

namespace profiles
{
TransitionProfile firstStrike();
TransitionProfile chainLink();
TransitionProfile blockFromLatch();
TransitionProfile blockLive();
TransitionProfile dodge();
TransitionProfile jump();
} // namespace profiles

// Loco-lockout state, owned here. Read by the per-frame movement gate.
float locoLockoutUntil();
void setLocoLockoutUntil(float t);

// Single entry point for "fire a one-shot with this profile." Reads
// profile, applies source-prep, kicks playOneShot. Inertialization
// is intentionally not enrolled by any game-side profile (see
// feedback_animation_harmony_rule.md); the engine retains the
// capability for future reuse but Selva Oscura never opts in.
// Lockout assignment is separate (caller knows the anchor —
// chain.cancel_window_close_at vs dodge end vs wall clock).
void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                            const TransitionProfile& profile, float start_seconds,
                            float playback_rate, selva::anim::PoseSampler& sampler,
                            const char* clip_key = "");

// Apply a profile's lockout strategy. Called after chain code has
// computed cancel_window_close_at for the just-fired attack.
void applyProfileLockout(const TransitionProfile& profile, float cancel_window_close_at);

} // namespace selva::combat
