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
    bool enroll_inertialization = true;
    Lockout lockout = Lockout::None;
    float lockout_seconds = 0.0f;
    selva::anim::PoseSampler::BodyMask mask = selva::anim::PoseSampler::BodyMask::Full;
    bool freeze_last = false;
};

namespace profiles
{
TransitionProfile firstStrike();
TransitionProfile chainLink();
TransitionProfile blockFromLatch();
TransitionProfile blockLive();
TransitionProfile dodge();
} // namespace profiles

// Loco-lockout state, owned here. Read by the per-frame movement gate.
float locoLockoutUntil();
void setLocoLockoutUntil(float t);

// Single entry point for "fire a one-shot with this profile." Reads
// profile, applies source-prep, optionally enrolls inertialization,
// kicks playOneShot. Lockout assignment is separate (caller knows the
// anchor — chain.cancel_window_close_at vs dodge end vs wall clock).
void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                            const TransitionProfile& profile, float start_seconds,
                            float playback_rate, selva::anim::PoseSampler& sampler,
                            const char* clip_key = "");

// Apply a profile's lockout strategy. Called after chain code has
// computed cancel_window_close_at for the just-fired attack.
void applyProfileLockout(const TransitionProfile& profile, float cancel_window_close_at);

} // namespace selva::combat
