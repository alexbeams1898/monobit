#include "combat/TransitionProfile.h"

#include "Tunables.h"
#include "WallClock.h"

namespace selva::combat
{

namespace
{
float sLocoLockoutUntil = 0.0f;
} // namespace

float locoLockoutUntil()
{
    return sLocoLockoutUntil;
}
void setLocoLockoutUntil(float t)
{
    sLocoLockoutUntil = t;
}

// Inertialization is intentionally NOT enrolled by any profile here.
// The PoseSampler engine retains the capability (requestInertialization
// + applyInertializationDecay) for future games or specific opt-in
// callers, but Selva Oscura never requests it. Reason: every Mixamo
// clip in our pipeline animates rapidly in its first 100-450ms
// (windup, recoil, raise), which is exactly the failure mode for
// inertialization's frozen-offset decay math — the offset overlays
// the clip's authored motion and produces visible foot drift.
// See feedback_animation_harmony_rule.md.

namespace profiles
{

TransitionProfile firstStrike()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.first_strike_blend_seconds;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}

TransitionProfile chainLink()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.combo_chain_blend_seconds;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}

TransitionProfile blockFromLatch()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}

TransitionProfile blockLive()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::SnapLocoToZero;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}

TransitionProfile dodge()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.lockout = TransitionProfile::Lockout::None;
    // Dodge isn't intent-driving a stance change; freeze loco so
    // the dodge fades back into the player's prior loco clip rather
    // than into whatever the SM picks during the dodge's flight.
    p.freeze_loco_during_one_shot = true;
    // Tracks the historical tun.dodge_cancel_fraction. Player can
    // chain another one-shot (roll, jump) past 65% of clip duration.
    p.cancel_fraction = 0.65f;
    return p;
}

TransitionProfile jump()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.blend_out_seconds = 0.20f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.lockout = TransitionProfile::Lockout::None;
    // Like dodge: not a stance change, fade back into the player's
    // prior loco.
    p.freeze_loco_during_one_shot = true;
    // Allow movement + chain past 65% — the back half is recovery/
    // landing, the player shouldn't be locked through it.
    p.cancel_fraction = 0.65f;
    return p;
}

} // namespace profiles

void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                            const TransitionProfile& profile, float start_seconds,
                            float playback_rate, selva::anim::PoseSampler& sampler,
                            const char* clip_key, float freeze_at_seconds)
{
    if (profile.source_prep == TransitionProfile::SourcePrep::SnapLocoToZero)
        sampler.setLocomotionClipTime(0.0f);
    selva::anim::PoseSampler::OneShotOptions opts;
    opts.freeze_last = profile.freeze_last;
    opts.freeze_at_seconds = freeze_at_seconds;
    opts.clip_key = clip_key;
    opts.freeze_loco_during_one_shot = profile.freeze_loco_during_one_shot;
    opts.cancel_fraction = profile.cancel_fraction;
    sampler.playOneShot(clip, profile.blend_in_seconds, profile.blend_out_seconds, profile.mask,
                        start_seconds, playback_rate, opts);
}

void applyProfileLockout(const TransitionProfile& profile, float cancel_window_close_at)
{
    const auto& tun = selva::tuning::current();
    float target = 0.0f;
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
