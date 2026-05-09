#include "combat/TransitionProfile.h"

#include "Tunables.h"
#include "WallClock.h"

namespace selva::combat
{

namespace
{
float sLocoLockoutUntil = 0.0f;
} // namespace

float locoLockoutUntil() { return sLocoLockoutUntil; }
void setLocoLockoutUntil(float t) { sLocoLockoutUntil = t; }

namespace profiles
{

TransitionProfile firstStrike()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.first_strike_blend_seconds;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}

TransitionProfile chainLink()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.combo_chain_blend_seconds;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}

TransitionProfile sprintFinisher()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.sprint_finisher_blend_in_seconds;
    p.enroll_inertialization = false;
    p.lockout = TransitionProfile::Lockout::WallClockSeconds;
    p.lockout_seconds = tun.sprint_finisher_lockout_seconds;
    return p;
}

TransitionProfile blockFromLatch()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}

TransitionProfile blockLive()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::SnapLocoToZero;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}

TransitionProfile dodge()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.enroll_inertialization = false;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}

} // namespace profiles

void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                            const TransitionProfile& profile, float start_seconds,
                            float playback_rate, selva::anim::PoseSampler& sampler)
{
    if (profile.source_prep == TransitionProfile::SourcePrep::SnapLocoToZero)
        sampler.setLocomotionClipTime(0.0f);
    if (profile.enroll_inertialization)
        sampler.requestInertialization(profile.blend_in_seconds);
    sampler.playOneShot(clip, profile.blend_in_seconds, profile.blend_out_seconds, profile.mask,
                        start_seconds, playback_rate, profile.freeze_last);
}

void applyProfileLockout(const TransitionProfile& profile, float cancel_window_close_at)
{
    const auto& tun = selva::tuning::current();
    float target = sLocoLockoutUntil;
    switch (profile.lockout)
    {
    case TransitionProfile::Lockout::None:
        return;
    case TransitionProfile::Lockout::CancelWindowClose:
        target = cancel_window_close_at + tun.attack_lockout_extension_seconds;
        break;
    case TransitionProfile::Lockout::WallClockSeconds:
        target = selva::wallClock() + profile.lockout_seconds;
        break;
    }
    if (target > sLocoLockoutUntil)
        sLocoLockoutUntil = target;
}

} // namespace selva::combat
