#include "gameplay/PerFrameTick.h"

#include "Engine.h"
#include "Tunables.h"
#include "WallClock.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalAssets.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/Skeleton.h"
#include "combat/AttackChain.h"
#include "combat/AttackResolution.h"
#include "combat/ChainObserver.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/PlayerEquipment.h"
#include "combat/PressMapping.h"
#include "combat/SpliceDiag.h"
#include "combat/TransitionProfile.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"
#include "gameplay/LocomotionStateMachine.h"
#include "gameplay/PlayerState.h"
#include "gameplay/TickState.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "render/WorldRenderer.h"
#include "ui/ComboHud.h"

#include <imgui.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <tracy/Tracy.hpp>

#define SDL_MAIN_HANDLED
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ozz/base/maths/soa_transform.h>

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <glad/glad.h>

// File-scope aliases mirroring main.cpp conventions so transplanted
// gameplay code compiles unchanged.
static selva::anim::Skeleton& sSkeleton = selva::anim::skeleton();
static selva::anim::SkeletalMesh& sPlayerMesh = selva::anim::playerMesh();
static selva::anim::ClipRegistry& sClips = selva::anim::clips();
static selva::anim::PoseSampler& sSampler = selva::anim::sampler();
static selva::anim::LocomotionConfig& sLocomotionConfig = selva::anim::locomotionConfig();
using selva::gameplay::PlayerState;
static PlayerState& sPlayer = selva::gameplay::player();
using selva::gameplay::CombatStance;
using selva::gameplay::CombatStanceFoot;
using selva::gameplay::LocomotionFrameOutput;
using selva::gameplay::LocomotionState;
using selva::gameplay::LocomotionStateMachine;
using selva::gameplay::loopClipForState;
using selva::gameplay::selectTransitionClip;
using selva::gameplay::tickLocomotionStateMachine;
static LocomotionStateMachine& sLocomotionSM = selva::gameplay::locomotionSM();
using selva::combat::AttackKind;
using selva::combat::BufferedPress;
using selva::combat::PendingFirstAction;
static BufferedPress& sBufferedRight = selva::combat::buffer(selva::combat::HandSide::Right);
static BufferedPress& sBufferedLeft = selva::combat::buffer(selva::combat::HandSide::Left);
static PendingFirstAction& sPendingFirstAction = selva::combat::pendingFirstAction();
using selva::combat::applyProfileLockout;
using selva::combat::combatLog;
using selva::combat::effectiveAttackPlaybackRate;
using selva::combat::isMovingLocoClip;
using selva::combat::isUnarmed;
using selva::combat::offHandCanBlock;
using selva::combat::SpliceDiag;
using selva::combat::TransitionProfile;
namespace profiles = selva::combat::profiles;
using selva::gameplay::wrapAngleSigned;
using selva::gameplay::yawFromGroundDir;
static selva::combat::WeaponClassRegistry& sWeaponClasses = selva::combat::weaponClasses();
static selva::combat::WeaponRegistry& sWeapons = selva::combat::weapons();
static selva::combat::PlayerEquipment& sEquipment = selva::combat::equipment();

// One-shot input edge detection. SDL's keyboard state is "is this key down
// right now"; for actions like the F1 panel toggle we need the rising
// edge — was up last frame, down this frame.
static bool sPrevF1 = false;
static bool sPrevLMB = false;
static bool sPrevRMB = false;
static bool sPrevF = false;
static bool sPrevGripToggle = false;

// Per-frame WASD prev-state for edge logging. The actual movement
// computation reads SDL_GetKeyboardState live; these only exist so
// the diagnostic trace can mark down → up → down events with
// timestamps.
static bool sPrevW = false;
static bool sPrevA = false;
static bool sPrevS = false;
static bool sPrevD = false;

// Space tap-vs-hold disambiguation:
//   * Tap (release within tun.dodge_tap_window) → dodge roll.
//   * Hold (past the window) → sprint state engages.
// We track when Space was first pressed and whether a dodge has been
// fired this press; that lets us avoid double-firing or double-engaging
// while the key is held down.
static bool sPrevSpace = false;
static float sSpaceHeldSeconds = 0.0f;
static bool sDodgeFiredThisPress = false;

// Dodge state. While a dodge is active, gameplay-driven movement is
// locked — the roll clip's authored hip motion drives the forward travel
// (root-motion-driven, the standard pattern for combat actions per
// AnimotionX research notes). The one-shot blend-out continues briefly
// after sDodgeActive flips false, but the player regains control then.
//
// We rotate the player's yaw to face sDodgeDir at fire-time so the
// clip's authored forward motion (clip-local +Z) lands in the world
// direction the player intended. No per-frame translation here — the
// roll clip's hip translation, plus the model matrix's yaw, takes the
// character where it needs to go.
static bool sDodgeActive = false;

// Post-dodge attack buffer. When LMB/RMB lands during a dodge, the
// press is recorded here instead of being dropped or firing on top
// of the rolling pose. As soon as the dodge clip ends naturally,
// the buffered press fires as a standard first-strike from the
// (now-settled) combat-idle pose. Lets the player chain rolls into
// attacks without timing the press exactly to the dodge's end frame.
struct PostDodgeAttackBuffer
{
    bool pending = false;
    selva::combat::HandSide hand = selva::combat::HandSide::Right;
    const char* button = "LMB";
    float buffered_at = 0.0f; // wall-clock when the press was queued
};
static PostDodgeAttackBuffer sPostDodgeAttack;

// Buffered dodge press. When Space is tap-released DURING an active
// one-shot (any attack, mid-recovery one-shot, etc.), the dodge would
// otherwise drop. Instead we record the snapshot here; the per-frame
// tick fires it as soon as the active hand's cancel window opens (or
// the one-shot fully ends). Survives chain advances: pressing Space
// mid-jab and chaining into hook keeps the buffer until hook's cancel
// window opens, then dodges. Drops on combo_input_buffer_seconds
// elapsed without firing.
struct BufferedDodgePress
{
    bool pending = false;
    glm::vec3 move_intent_at_press{0.0f};
    float buffered_at = 0.0f;
};
static BufferedDodgePress sBufferedDodge;

static float sDodgeElapsed = 0.0f;
static float sDodgeDuration = 0.0f;
// Direction flag set at dodge fire time. Forward roll: yaw is set to
// move-intent direction; clip-local +Z hip motion lands as world-frame
// forward travel. Backstep: yaw is unchanged; clip-local -Z hip motion
// (the standing_dodge_backward clip authors backward travel) lands as
// world-frame backward. Backsteps also disable mid-roll yaw steering —
// you commit to a backstep direction.
static bool sDodgeIsBackstep = false;
// Active playback rate of the in-flight dodge clip. Scales the wall-
// clock duration the dodge gates input lock and is used to convert
// clip-time elapsed into wall-clock elapsed for the same gate.
static float sDodgePlaybackRate = 1.0f;

// AttackKind / BufferedPress / PendingFirstAction + per-hand
// BufferedPress singletons + tickChainExpiry live in
// combat/AttackChain.{h,cpp}. Chain state + per-hand cancel windows
// live in combat/ChainObserver.{h,cpp}.

// Wall-clock seconds since the game started — owned by
// selva::wallClock() (WallClock.h). Advanced once per frame at the
// top of selvaPerFrame via selva::advanceWallClock(dt).

// Loco-lockout state owned by combat/TransitionProfile.{h,cpp}. The
// per-frame movement gate reads selva::combat::locoLockoutUntil().

// WASD debounce. SM commits to a wasd_intent state change only after
// the raw value has disagreed continuously for wasd_debounce_seconds.
// Stops sub-blend-window rapid taps from spawning back-to-back loco
// crossfades. The high-res trace showed alternating loco-pick events
// ~30-40ms apart during rapid mashing — well under any reasonable
// blend duration. Even with reverse-blend protection, that many
// transitions per second produces visible jitter.
static bool sStableWasdIntent = false;
// -1 sentinel = no pending disagreement. Set to wall-clock time on
// the first frame of disagreement, cleared back to -1 when raw and
// stable agree OR when the debounce elapses and we commit.
static float sWasdDisagreeStartedAt = -1.0f;

// Held-block state. When the off-hand item supports blocking
// (currently: any weapon whose class id is "buckler") and RMB is
// held, the player enters a blocking pose:
//   * On press-edge: fire `sword_and_shield_block` one-shot (the
//     "raise shield" beat); set sBlockingActive = true.
//   * While held: override the locomotion clip to `sword_and_shield_block_idle`
//     (a stationary held-block loop) so the body persists in the
//     defensive pose past the block_1 one-shot's duration.
//   * On release-edge: fire `sword_and_shield_block_2` one-shot (the
//     "lower shield" beat); set sBlockingActive = false. Locomotion
//     resumes normal selection on the next frame.
//
// This is a one-button stateful action layered on top of the generic
// attack-chain machinery, not a chain of its own. RMB tap-fire
// (light/heavy attack) is suppressed when the off-hand is a blocker —
// the off-hand item's "purpose" is defense, and that's what RMB does
// for it.
static bool sBlockingActive = false;

// Input → attack mapping helper. Combines the equipped weapon, its class,
// In-game tuning panel toggle. Off by default; F1 flips it.
static bool sShowTuningPanel = false;
// Combat debug toggle + log file owned by combat/CombatLog.{h,cpp}.
// Aliases here keep the heavy call-site count (100+ combatLog uses)
// readable without `selva::combat::` everywhere.
using selva::combat::combatLog;

// Cached previous-frame right-hand position. Updated at the bottom of
// selvaPerFrame after the sampler has produced this frame's pose.
// Used by the chain-fire splice diagnostic to compute the live hand's
// velocity vector (this frame minus last frame) at the moment of
// splice — that direction tells us whether inertialization's pose-
// decay smears parallel to the new clip's motion (invisible) or
// orthogonal to it (visible jerk).
// Last right-hand cache lives in combat/SpliceDiag.{h,cpp}.

// ---------------------------------------------------------------------------
// Animation debug — when sDebugClipName is non-empty, the per-frame update
// bypasses the entire gameplay state machine (locomotion selection,
// attacks, start-walk, movement) and feeds the named clip straight into
// the sampler. Lets us preview any clip in-game in isolation, with no
// interference from blending-with-locomotion or one-shot logic, so we
// can side-by-side compare against the browser previewer.
//
// The CSV recorder captures world-space joint translations every frame
// while sDebugRecording is true, then writes them to a file when the
// recording duration elapses. The browser-side previewer has a matching
// "Export bone CSV" button that produces the same shape; a python diff
// script compares the two.
// ---------------------------------------------------------------------------
static std::string sDebugClipName;
// The locomotion clip name (registry key) selected on the previous
// frame — read this frame by combat-fire code that runs BEFORE the
// per-frame SM pick. The ozz Animation::name() returns "mixamo.com"
// for every Mixamo-exported clip, so it can't be used to identify
// which loco clip is playing. This static IS the registry key
// ("unarmed_combat_idle", "walking", "running", etc.).
static std::string sLastLocoClipName = "standard_idle";
static bool sDebugLoop = true;

struct DebugBoneSample
{
    float time_s = 0.0f;
    std::vector<glm::vec3> joints; // one entry per skeleton joint
};
static bool sDebugRecording = false;
static float sDebugRecordElapsed = 0.0f;
static float sDebugRecordDuration = 0.0f;
static std::string sDebugRecordClipName;
static std::vector<DebugBoneSample> sDebugRecordSamples;

// "Armed" flag: when true, the next time the dodge fires, start a CSV
// recording for the duration of that dodge. Used to measure the
// playOneShot path's bone trajectories — comparing against the F1
// debug-clip recording (which goes through update() as a locomotion
// clip) reveals whether the two paths produce different poses, even
// though they should be sampling the same clip. The CSV lands as
// debug_bones_game_dodge_<clip>.csv to disambiguate from the F1 recording.
static bool sDebugRecordArmedNextDodge = false;

// Frame screenshot capture. When sFrameCaptureActive, every rendered
// frame is written as a PNG into sFrameCaptureDir. Used to inspect
// what the camera is *actually* showing during a dodge, frame by
// frame, instead of relying on description. The recording stops after
// sFrameCaptureDuration seconds; the user/agent then reviews the PNGs.
static bool sFrameCaptureArmedNextDodge = false;
// Same machinery, but armed for the next attack chain. The duration
// scales by the inverse of attack_playback_rate so a slow-mo chain
// captures over enough wall-clock seconds to cover the whole sequence.
static bool sFrameCaptureArmedNextChain = false;
static bool sFrameCaptureActive = false;
static int sFrameCaptureCounter = 0;
static float sFrameCaptureElapsed = 0.0f;
static float sFrameCaptureDuration = 0.0f;
static std::string sFrameCaptureDir;

// Tunables.json path stays here (used only by tuning save). Weapon
// classes / weapons / equipment registries + their config paths live
// in combat/CombatData.{h,cpp}.
static const std::string kTunablesPath = "config/tunables.json";
// (sWeaponClasses / sWeapons / sEquipment aliases declared in prelude.)

// Pose-match start time for a one-shot fired from the live locomotion
// track. Scans the new clip's first `window_seconds` for the frame
// whose hand pose is closest to the current loco-clip pose, returns
// that clip-time. Lets ANY one-shot (attack, dodge, block, future
// jump) bridge cleanly out of combat-idle / standard-idle / walking
// without snapping the legs to the new clip's authored t=0 pose.
//
// The attack path used to inline this scan; extracted so dodge and
// block can use the same logic without duplicating the joint setup.
// Returns 0.0f when no scan source resolves (loco clip missing,
// joint indices unavailable) — caller treats that as "play from t=0
// like before."
// SpliceDiag + capture/log + poseMatchStartFromLoco + isMovingLocoClip
// + lastRightHandPos cache live in combat/SpliceDiag.{h,cpp}. Wrappers
// here avoid threading sampler/clips/last-loco-name through every site.
using selva::combat::isMovingLocoClip;
using selva::combat::SpliceDiag;
static SpliceDiag captureSpliceDiag()
{
    return selva::combat::captureSpliceDiag(sSampler);
}
static void logSpliceDiag(const SpliceDiag& d, const selva::anim::AnimationClip& new_clip,
                          float start_seconds, const char* prefix)
{
    selva::combat::logSpliceDiag(d, new_clip, start_seconds, prefix, sSampler);
}
static float poseMatchStartFromLoco(const selva::anim::AnimationClip& new_clip,
                                    float window_seconds)
{
    return selva::combat::poseMatchStartFromLoco(new_clip, window_seconds, sSampler, sClips,
                                                 sLastLocoClipName);
}

// effectiveAttackPlaybackRate lives in combat/CombatData.{h,cpp}.
using selva::combat::effectiveAttackPlaybackRate;

// TransitionProfile + 6 profiles + fireOneShotWithProfile +
// applyProfileLockout live in combat/TransitionProfile.{h,cpp}.
using selva::combat::applyProfileLockout;
using selva::combat::TransitionProfile;
namespace profiles = selva::combat::profiles;
static void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                                   const TransitionProfile& profile, float start_seconds,
                                   float playback_rate, const char* clip_key = "")
{
    selva::combat::fireOneShotWithProfile(clip, profile, start_seconds, playback_rate, sSampler,
                                          clip_key);
}

// Window size + onWindowResize callback owned by render/Camera.{h,cpp}.

// ---------------------------------------------------------------------------
// Yaw helpers
// ---------------------------------------------------------------------------

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
// Used by smooth turn-to-direction logic; if we're at 170° and target is
// -170°, the unwrapped delta is -340° (going the long way), but the wrapped
// delta is +20° (going the short way through 180°).
// wrapAngleSigned + yawFromGroundDir live in gameplay/PlayerState.{h,cpp}.
using selva::gameplay::wrapAngleSigned;
using selva::gameplay::yawFromGroundDir;

// resolveAttackCancelOpenTimes lives in combat/AttackResolution.{h,cpp}.
// Wrap so the existing call sites (init, F1 panel re-resolve buttons)
// don't need to thread the registries explicitly.
static void resolveAttackCancelOpenTimes()
{
    selva::combat::resolveAttackCancelOpenTimes(sWeaponClasses, sClips, sSampler);
}

// ---------------------------------------------------------------------------
// Per-frame update — runs at wall-clock rate.
// ---------------------------------------------------------------------------

// Drain raw mouse motion this frame and apply it to camera yaw + pitch
// (clamped). Suppressed while the tuning panel is open so mouse drags
// on sliders don't aim the camera.
static void tickMouseLook(const selva::tuning::Tunables& tun)
{
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    if (sShowTuningPanel)
        return;
    float yaw = selva::render::cameraYaw();
    float pitch = selva::render::cameraPitch();
    yaw -= static_cast<float>(mdx) * tun.mouse_sensitivity;
    pitch -= static_cast<float>(mdy) * tun.mouse_sensitivity;
    if (pitch < tun.pitch_min)
        pitch = tun.pitch_min;
    if (pitch > tun.pitch_max)
        pitch = tun.pitch_max;
    selva::render::setCameraYaw(yaw);
    selva::render::setCameraPitch(pitch);
}

// Rising-edge F1 toggles the tuning panel and flips relative-mouse
// capture. Drains accumulated mouse motion on re-capture so the
// camera doesn't snap.
static void tickF1TuningPanelToggle(const Uint8* keys)
{
    const bool f1Now = keys[SDL_SCANCODE_F1] != 0;
    if (f1Now && !sPrevF1)
    {
        sShowTuningPanel = !sShowTuningPanel;
        SDL_SetRelativeMouseMode(sShowTuningPanel ? SDL_FALSE : SDL_TRUE);
        SDL_GetRelativeMouseState(nullptr, nullptr);
    }
    sPrevF1 = f1Now;
}

// Pose-match start time for a chain-link splice: source is the
// outgoing one-shot's current frame, joint set is hands. Returns -1
// when source clip / joints unresolvable (caller falls back to
// loco-derived start).
static float chainLinkPoseMatchStart(const selva::anim::AnimationClip& clip,
                                     const char* prev_clip_name, float prev_clip_time)
{
    const auto* prev_clip = sClips.get(prev_clip_name);
    if (prev_clip == nullptr || !prev_clip->isLoaded())
        return -1.0f;
    const int rh = sSampler.findJoint("mixamorig:RightHand");
    const int lh = sSampler.findJoint("mixamorig:LeftHand");
    std::vector<int> joints;
    if (rh >= 0)
        joints.push_back(rh);
    if (lh >= 0)
        joints.push_back(lh);
    if (joints.empty())
        return -1.0f;
    return sSampler.clipPoseMatchTime(*prev_clip, prev_clip_time, clip, joints, 0.0f, 0.50f);
}

// Look up the WeaponAttack entry whose `clip` matches `clip_name`
// in the equipped weapon's technique tree. Returns nullptr if the
// clip isn't a configured attack (e.g. a generic dodge/block clip).
static const selva::combat::WeaponAttack* findAttackForClip(const char* clip_name)
{
    if (sEquipment.right == nullptr || sEquipment.right->cls == nullptr)
        return nullptr;
    const auto& aset = (sEquipment.grip == selva::combat::Grip::TwoHanded)
                           ? sEquipment.right->cls->two_handed
                           : sEquipment.right->cls->one_handed;
    const std::vector<const std::vector<selva::combat::WeaponTechnique>*> tech_lists{
        &aset.light, &aset.heavy, &aset.running};
    for (const auto* techs : tech_lists)
        for (const auto& tech : *techs)
            for (const auto& atk : tech.attacks)
                if (atk.clip == clip_name)
                    return &atk;
    return nullptr;
}

// Look up the per-attack `blend_out_seconds` override for `clip_name`.
// Returns negative if the clip isn't an attack or didn't override.
static float perAttackBlendOutOverride(const char* clip_name)
{
    const auto* atk = findAttackForClip(clip_name);
    return (atk != nullptr) ? atk->blend_out_seconds : -1.0f;
}

// Set the per-hand cancel window (open / close) on the chain
// observer, scaled by the active playback rate so the band lines up
// with the sped-up clip.
//
// Cancel-open seconds source priority:
//   1. Per-attack `resolved_cancel_open_seconds` from the technique
//      tree (auto-detected at load time via the joint-motion-end
//      scan, or JSON-overridden). This honors the actual commit
//      window of THIS attack — running attacks lunge committedly
//      for the first ~70% so motion-end is late, while jabs settle
//      around 40%.
//   2. 40% of clip duration as a fallback for clips that aren't a
//      configured attack (generic dodge, block, etc.).
//
// The earlier flat-40% rule cancelled running attacks well before
// their lunge committed, letting RMB/dodge interrupt the
// forward-momentum phase visibly.
static void setHandCancelWindow(selva::combat::HandSide hand, const char* clip_name,
                                const selva::anim::AnimationClip& clip, float rate,
                                float combo_input_buffer_seconds)
{
    float cancel_open = (clip.duration() > 0.0f) ? clip.duration() * 0.40f : 0.30f;
    const auto* atk = findAttackForClip(clip_name);
    if (atk != nullptr && atk->resolved_cancel_open_seconds >= 0.0f)
        cancel_open = atk->resolved_cancel_open_seconds;
    const float cancel_close = cancel_open + combo_input_buffer_seconds;
    selva::combat::setCancelWindow(hand, selva::wallClock() + cancel_open / rate,
                                   selva::wallClock() + cancel_close / rate);
}

// Fire a clip directly: pose-match against the current state, pick a
// profile (chainLink vs firstStrike), apply any per-attack blend_out
// override, kick off the one-shot, and set the hand's cancel window.
// No chain bookkeeping. Returns true on successful fire.
static bool fireClipForHand(selva::combat::HandSide hand, const char* clip_name,
                            float combo_input_buffer_seconds)
{
    const auto* clip = sClips.get(clip_name);
    if (clip == nullptr || !clip->isLoaded())
        return false;
    const bool one_shot_active = sSampler.isOneShotActive();
    const auto fd_pre = sSampler.frameDiagnostics();

    float pose_matched_start = -1.0f;
    if (one_shot_active && fd_pre.one_shot_name != nullptr)
        pose_matched_start =
            chainLinkPoseMatchStart(*clip, fd_pre.one_shot_name, fd_pre.one_shot_time);
    else
        pose_matched_start = poseMatchStartFromLoco(*clip, 0.30f);
    const float start_seconds = (pose_matched_start >= 0.0f) ? pose_matched_start : 0.0f;

    TransitionProfile profile = one_shot_active ? profiles::chainLink() : profiles::firstStrike();
    profile.lockout = TransitionProfile::Lockout::None;
    const float blend_override = perAttackBlendOutOverride(clip_name);
    if (blend_override >= 0.0f)
        profile.blend_out_seconds = blend_override;

    const float rate = effectiveAttackPlaybackRate(hand);
    fireOneShotWithProfile(*clip, profile, start_seconds, rate, clip_name);
    setHandCancelWindow(hand, clip_name, *clip, rate, combo_input_buffer_seconds);
    combatLog("[combat:fire] hand=%s clip=%s dur=%.3fs start=%.3fs rate=%.2f one_shot_active=%d\n",
              (hand == selva::combat::HandSide::Right) ? "R" : "L", clip_name, clip->duration(),
              start_seconds, rate, one_shot_active ? 1 : 0);
    return true;
}

// Record the press's wall-clock against the cancel window's center +
// half-width — feeds the rhythm-combo bonus calc.
static void recordPressForCancelWindow(selva::combat::HandSide hand, const char* button_to_fire,
                                       float now)
{
    const auto& w = selva::combat::cancelWindow(hand);
    const float center = 0.5f * (w.open_at + w.close_at);
    const float half = 0.5f * (w.close_at - w.open_at);
    selva::combat::recordPress(sEquipment, button_to_fire, now, center, half);
}

// Press during dodge → buffer the press for post-dodge handoff.
static void bufferPostDodgeAttack(selva::combat::HandSide hand, const char* button)
{
    sPostDodgeAttack.pending = true;
    sPostDodgeAttack.hand = hand;
    sPostDodgeAttack.button = button;
    sPostDodgeAttack.buffered_at = selva::wallClock();
    combatLog("[combat:rhythm %.4fs] press BUFFERED (dodge active)\n", selva::wallClock());
}

// Press while a one-shot is in flight: only fire if it advances a
// chain inside the cancel window. Otherwise buffer for post-clip
// replay. Returns true if a chain advance fired.
static bool tryChainAdvanceFire(selva::combat::HandSide hand, const char* button,
                                const selva::combat::PressModifiers& mods, float now,
                                float combo_input_buffer_seconds)
{
    const auto& w = selva::combat::cancelWindow(hand);
    const bool inside_window = w.close_at > w.open_at && now >= w.open_at && now <= w.close_at;
    if (!inside_window)
        return false;
    bool is_chain_advance = false;
    const char* clip_name = selva::combat::clipForButton(sEquipment, hand, button, mods, now,
                                                         w.open_at, w.close_at, &is_chain_advance);
    if (!is_chain_advance || clip_name == nullptr)
        return false;
    if (!fireClipForHand(hand, clip_name, combo_input_buffer_seconds))
        return false;
    recordPressForCancelWindow(hand, button, now);
    return true;
}

// Press handler: route the press through dodge-buffer, chain-advance,
// fresh-fire, or buffer-for-replay. Returns true if a clip fired this
// call (so the caller can mark combat_input_this_frame).
static bool tryAttackInputDispatch(selva::combat::HandSide hand, bool press_edge_this_frame,
                                   const char* button, const selva::combat::PressModifiers& mods,
                                   float combo_input_buffer_seconds)
{
    BufferedPress& buf = (hand == selva::combat::HandSide::Right) ? sBufferedRight : sBufferedLeft;
    if (press_edge_this_frame && sDodgeActive)
    {
        bufferPostDodgeAttack(hand, button);
        return false;
    }
    if (press_edge_this_frame)
    {
        const float now = selva::wallClock();
        if (sSampler.isOneShotActive())
        {
            if (tryChainAdvanceFire(hand, button, mods, now, combo_input_buffer_seconds))
                return true;
            buf.pending = true;
            buf.button = button;
            buf.buffered_at = now;
            combatLog("[combat:rhythm] BUFFERED (one-shot in flight, no chain advance; %s)\n",
                      button);
            return false;
        }
        // No one-shot active: fire mapped clip directly.
        const auto& w = selva::combat::cancelWindow(hand);
        const char* clip_name = selva::combat::clipForButton(sEquipment, hand, button, mods, now,
                                                             w.open_at, w.close_at);
        if (clip_name == nullptr)
        {
            combatLog("[combat:rhythm] press DROPPED (no clip mapping for %s)\n", button);
            return false;
        }
        if (fireClipForHand(hand, clip_name, combo_input_buffer_seconds))
        {
            recordPressForCancelWindow(hand, button, now);
            return true;
        }
        return false;
    }

    // No fresh press: replay buffered if one-shot just ended.
    if (buf.pending && !sSampler.isOneShotActive())
    {
        const float now = selva::wallClock();
        if (now - buf.buffered_at > combo_input_buffer_seconds)
        {
            buf.pending = false;
            return false;
        }
        const auto& w = selva::combat::cancelWindow(hand);
        const char* clip_name = selva::combat::clipForButton(sEquipment, hand, buf.button, mods,
                                                             now, w.open_at, w.close_at);
        const bool fired =
            clip_name != nullptr && fireClipForHand(hand, clip_name, combo_input_buffer_seconds);
        if (fired)
            recordPressForCancelWindow(hand, buf.button, now);
        buf.pending = false;
        return fired;
    }
    return false;
}

// Build a contact-sheet PNG from the captured per-frame screenshots.
// Strides frames so the sheet has at most kMaxCells (≈30); skips the
// composite entirely if total bytes would exceed 64MB. Individual
// PNGs stay on disk regardless.
static void writeFrameCaptureContactSheet(int n_frames, const std::string& dir)
{
    if (n_frames <= 0)
        return;
    constexpr int kMaxCells = 30;
    const int stride = std::max(1, (n_frames + kMaxCells - 1) / kMaxCells);
    const int n_cells = (n_frames + stride - 1) / stride;
    constexpr int kMaxCols = 3;
    const int cols = std::min(
        kMaxCols,
        std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n_cells) * 1.78f)))));
    const int rows = (n_cells + cols - 1) / cols;
    char path0[512];
    std::snprintf(path0, sizeof(path0), "%s/frame_0000.png", dir.c_str());
    int cell_w = 0;
    int cell_h = 0;
    int cell_ch = 0;
    stbi_uc* probe = stbi_load(path0, &cell_w, &cell_h, &cell_ch, 3);
    if (probe == nullptr || cell_w <= 0 || cell_h <= 0)
        return;
    stbi_image_free(probe);
    constexpr int kBorder = 2;
    const int sheet_w = cols * (cell_w + kBorder) + kBorder;
    const int sheet_h = rows * (cell_h + kBorder) + kBorder;
    const std::size_t bytes =
        static_cast<std::size_t>(sheet_w) * static_cast<std::size_t>(sheet_h) * 3;
    if (bytes > 64ull * 1024 * 1024)
    {
        std::fprintf(stderr,
                     "[frame-capture] sheet would be %zu MB (%dx%d %dx%d cells); "
                     "skipping. Individual PNGs at %s/\n",
                     bytes / (1024ULL * 1024ULL), sheet_w, sheet_h, cols, rows, dir.c_str());
        return;
    }
    std::vector<unsigned char> sheet(bytes, 32);
    int cell_idx = 0;
    for (int i = 0; i < n_frames; i += stride)
    {
        char fp[512];
        std::snprintf(fp, sizeof(fp), "%s/frame_%04d.png", dir.c_str(), i);
        int fw = 0;
        int fh = 0;
        int fc = 0;
        stbi_uc* img = stbi_load(fp, &fw, &fh, &fc, 3);
        if (img == nullptr || fw != cell_w || fh != cell_h)
        {
            if (img != nullptr)
                stbi_image_free(img);
            ++cell_idx;
            continue;
        }
        const int gx = cell_idx % cols;
        const int gy = cell_idx / cols;
        const int x0 = kBorder + gx * (cell_w + kBorder);
        const int y0 = kBorder + gy * (cell_h + kBorder);
        for (int y = 0; y < cell_h; ++y)
        {
            const std::size_t dst_off = static_cast<std::size_t>((y0 + y) * sheet_w + x0) * 3;
            const std::size_t src_off = static_cast<std::size_t>(y * cell_w) * 3;
            std::memcpy(&sheet[dst_off], &img[src_off], static_cast<std::size_t>(cell_w) * 3);
        }
        stbi_image_free(img);
        ++cell_idx;
    }
    char sheet_path[512];
    std::snprintf(sheet_path, sizeof(sheet_path), "%s/_contact_sheet.png", dir.c_str());
    stbi_write_png(sheet_path, sheet_w, sheet_h, 3, sheet.data(), sheet_w * 3);
    std::fprintf(stderr,
                 "[frame-capture] contact sheet: %s (%dx%d, %d cells from %d "
                 "frames stride=%d, %dx%d grid)\n",
                 sheet_path, sheet_w, sheet_h, n_cells, n_frames, stride, cols, rows);
}

// Tick the frame-capture timer; once duration elapses, emit summary
// + contact sheet and clear the active flag.
static void tickFrameCapture(float dt)
{
    if (!sFrameCaptureActive)
        return;
    sFrameCaptureElapsed += dt;
    if (sFrameCaptureElapsed < sFrameCaptureDuration)
        return;
    std::fprintf(stderr, "[frame-capture] wrote %d frames to %s/\n", sFrameCaptureCounter,
                 sFrameCaptureDir.c_str());
    writeFrameCaptureContactSheet(sFrameCaptureCounter, sFrameCaptureDir);
    sFrameCaptureActive = false;
}

// Log one-shot state transitions for post-dodge handoff visibility.
// The active state is cached in a local static so the diff fires on
// edge changes only.
static void logOneShotStateTransitions()
{
    static bool sPrevOneShotActive = false;
    const bool now = sSampler.isOneShotActive();
    if (sPrevOneShotActive != now && selva::combat::isCombatDebugEnabled())
    {
        const auto fd = sSampler.frameDiagnostics();
        combatLog("[combat:one-shot %.4fs] active=%d weight=%.3f phase=%d\n", selva::wallClock(),
                  now ? 1 : 0, fd.one_shot_weight, fd.one_shot_phase);
    }
    sPrevOneShotActive = now;
}

// Post-dodge attack handoff. Once the dodge has fully ended AND the
// one-shot has fully blended out, fire the buffered attack as a
// first-strike from the (now-settled) combat-idle pose. Returns true
// if a clip fired (caller marks combat_input_this_frame).
static bool tryFirePostDodgeAttack(bool shift_held, float combo_input_buffer_seconds)
{
    if (!sPostDodgeAttack.pending || sDodgeActive || sSampler.isOneShotActive())
        return false;
    const float wait_seconds = selva::wallClock() - sPostDodgeAttack.buffered_at;
    const selva::combat::PressModifiers mods{shift_held, sPlayer.sprinting};
    const auto& dw = selva::combat::cancelWindow(sPostDodgeAttack.hand);
    const char* clip_name =
        selva::combat::clipForButton(sEquipment, sPostDodgeAttack.hand, sPostDodgeAttack.button,
                                     mods, selva::wallClock(), dw.open_at, dw.close_at);
    bool fired = false;
    if (clip_name != nullptr &&
        fireClipForHand(sPostDodgeAttack.hand, clip_name, combo_input_buffer_seconds))
    {
        fired = true;
        combatLog("[combat:rhythm %.4fs] post-dodge attack FIRED (waited %.3fs since press)\n",
                  selva::wallClock(), wait_seconds);
    }
    sPostDodgeAttack.pending = false;
    return fired;
}

// Smooth-turn the player's yaw toward the move-intent direction at
// `turn_rate` per frame. No-op when movement is locked or intent is
// zero. Yaw stays gameplay-driven; translation is read from the
// sampler's hip delta after this.
static void tickPlayerYaw(const glm::vec3& moveIntent, bool movement_locked, float turn_rate,
                          float dt)
{
    if (movement_locked || glm::length(moveIntent) <= 0.0001f)
        return;
    const glm::vec3 dir = glm::normalize(moveIntent);
    const float targetYaw = yawFromGroundDir(dir);
    float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
    const float maxStep = turn_rate * dt;
    if (delta > maxStep)
        delta = maxStep;
    else if (delta < -maxStep)
        delta = -maxStep;
    sPlayer.yaw += delta;
}

// Edge-log WASD/LMB/RMB transitions for repro reconstruction. Gated
// on the combat-debug switch.
static void logInputEdges(const Uint8* keys, bool press_lmb, bool release_lmb, bool press_rmb,
                          bool release_rmb)
{
    if (!selva::combat::isCombatDebugEnabled())
        return;
    const bool wNow = keys[SDL_SCANCODE_W] != 0;
    const bool aNow = keys[SDL_SCANCODE_A] != 0;
    const bool sNow = keys[SDL_SCANCODE_S] != 0;
    const bool dNow = keys[SDL_SCANCODE_D] != 0;
    if (wNow != sPrevW)
        combatLog("[input %.4fs] W %s\n", selva::wallClock(), wNow ? "DOWN" : "UP");
    if (aNow != sPrevA)
        combatLog("[input %.4fs] A %s\n", selva::wallClock(), aNow ? "DOWN" : "UP");
    if (sNow != sPrevS)
        combatLog("[input %.4fs] S %s\n", selva::wallClock(), sNow ? "DOWN" : "UP");
    if (dNow != sPrevD)
        combatLog("[input %.4fs] D %s\n", selva::wallClock(), dNow ? "DOWN" : "UP");
    if (press_lmb)
        combatLog("[input %.4fs] LMB DOWN\n", selva::wallClock());
    if (release_lmb)
        combatLog("[input %.4fs] LMB UP\n", selva::wallClock());
    if (press_rmb)
        combatLog("[input %.4fs] RMB DOWN\n", selva::wallClock());
    if (release_rmb)
        combatLog("[input %.4fs] RMB UP\n", selva::wallClock());
    sPrevW = wNow;
    sPrevA = aNow;
    sPrevS = sNow;
    sPrevD = dNow;
}

// F press handler: toggle combat stance, or flip stance foot when
// shift-held and already in CombatReady. Returns true if the press
// should mark combat_input_this_frame (entry into CombatReady).
static bool tryHandleStanceTogglePress(bool press_f, bool shift_held)
{
    if (!press_f)
        return false;
    if (sLocomotionSM.combat_stance == CombatStance::Peaceful)
        return true; // SM enters CombatReady
    if (shift_held)
    {
        sLocomotionSM.stance_foot = (sLocomotionSM.stance_foot == CombatStanceFoot::Default)
                                        ? CombatStanceFoot::Mirror
                                        : CombatStanceFoot::Default;
    }
    else
    {
        sLocomotionSM.stance_active_until = 0.0f;
        sLocomotionSM.combat_stance = CombatStance::Peaceful;
    }
    return false;
}

// Fire a block one-shot. Trusts the weapon class's leading-idle trim
// when set; otherwise pose-matches against the live loco track.
// `loco_settled` selects the blockFromLatch vs blockLive profile.
static void fireBlockOneShot(bool loco_settled, const char* clip_name, bool freeze_last)
{
    const auto* clip = sClips.get(clip_name);
    if (clip == nullptr || !clip->isLoaded())
        return;
    const int rh = sSampler.findJoint("mixamorig:RightHand");
    const auto fd_pre = sSampler.frameDiagnostics();
    const glm::vec3 live_pre = (rh >= 0) ? sSampler.jointWorldPos(rh) : glm::vec3(0);
    float block_start = 0.0f;
    if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr &&
        sEquipment.right->cls->block_clip_start_seconds > 0.0f)
        block_start = sEquipment.right->cls->block_clip_start_seconds;
    else
        block_start = poseMatchStartFromLoco(*clip, 0.30f);
    TransitionProfile profile = loco_settled ? profiles::blockFromLatch() : profiles::blockLive();
    profile.freeze_last = freeze_last;
    fireOneShotWithProfile(*clip, profile, block_start, /*playback_rate=*/1.0f, clip_name);
    sBlockingActive = true;

    const glm::vec3 block_t0 =
        (rh >= 0) ? sSampler.sampleJointWorldPos(*clip, 0.0f, rh) : glm::vec3(0);
    const glm::vec3 d = block_t0 - live_pre;
    combatLog("[combat:block-fire] mode=%s  loco=%s clip_t=%.3fs  "
              "RH live=(%.3f,%.3f,%.3f)  block_t0=(%.3f,%.3f,%.3f)  delta=|%.3fm|\n",
              loco_settled ? "SETTLED" : "SNAP",
              fd_pre.loco_current_name ? fd_pre.loco_current_name : "(none)",
              fd_pre.loco_current_time, live_pre.x, live_pre.y, live_pre.z, block_t0.x, block_t0.y,
              block_t0.z, glm::length(d));
}

// Resolve which block clip (if any) RMB should fire given the
// current equipment + modifiers. Buckler always blocks; unarmed +
// shift gestures block with freeze-last. Returns nullptr otherwise
// (caller treats RMB as a normal attack input).
static const char* resolveBlockClip(bool shift_held, bool& freeze_last)
{
    freeze_last = false;
    if (offHandCanBlock(sEquipment))
        return "sword_and_shield_block";
    if (isUnarmed(sEquipment) && shift_held)
    {
        freeze_last = true;
        return "unarmed_block";
    }
    return nullptr;
}

// RMB → block routing. Press from Peaceful arms a delayed first-
// action (so loco settles into combat-idle); subsequent press fires
// directly. Release ends a held block. Returns true if any combat
// input fired this call.
static bool tickBlockingFromRMB(const char* block_clip_name, bool block_freeze_last, bool press_rmb,
                                bool release_rmb, float combat_entry_delay_seconds)
{
    bool fired = false;
    if (press_rmb)
    {
        const bool from_peaceful = (sLocomotionSM.combat_stance == CombatStance::Peaceful);
        if (from_peaceful && !sPendingFirstAction.active)
        {
            sPendingFirstAction.active = true;
            sPendingFirstAction.block_clip = block_clip_name;
            sPendingFirstAction.block_freeze_last = block_freeze_last;
            sPendingFirstAction.fire_at = selva::wallClock() + combat_entry_delay_seconds;
            fired = true;
        }
        else if (!sPendingFirstAction.active)
        {
            fireBlockOneShot(false, block_clip_name, block_freeze_last);
            fired = true;
        }
    }
    else if (release_rmb && sBlockingActive)
    {
        sBlockingActive = false;
        sSampler.releaseOneShot();
        fired = true;
    }
    return fired;
}

// Fire a Peaceful→CombatReady block-latch when its delay has elapsed.
// Returns true if fired (caller marks combat_input_this_frame).
static bool tickPendingBlockLatch()
{
    if (!sPendingFirstAction.active || selva::wallClock() < sPendingFirstAction.fire_at)
        return false;
    fireBlockOneShot(true, sPendingFirstAction.block_clip, sPendingFirstAction.block_freeze_last);
    sPendingFirstAction.active = false;
    return true;
}

// Arm a CSV bone-trajectory recording for the just-fired dodge.
// Output is prefixed `dodge_` to disambiguate from F1-clip-debug
// recordings of the same clip.
static void armDodgeCsvRecordingIfRequested(float dodge_duration)
{
    if (!sDebugRecordArmedNextDodge || sDebugRecording)
        return;
    sDebugRecording = true;
    sDebugRecordElapsed = 0.0f;
    sDebugRecordDuration = dodge_duration;
    sDebugRecordClipName = "dodge_stand_to_roll";
    sDebugRecordSamples.clear();
    sDebugRecordSamples.reserve(static_cast<std::size_t>(dodge_duration * 65.0f));
    sDebugRecordArmedNextDodge = false;
    std::fprintf(stderr, "[anim-debug] recording dodge for %.2fs\n", dodge_duration);
}

// Arm a per-frame PNG capture for the just-fired dodge. Output to
// frame_capture/ (created if missing). Captures 0.3s past dodge end
// to see the recovery-to-idle blend.
static void armDodgeFrameCaptureIfRequested(float dodge_duration)
{
    if (!sFrameCaptureArmedNextDodge || sFrameCaptureActive)
        return;
    sFrameCaptureDir = "frame_capture";
    std::error_code ec;
    std::filesystem::create_directories(sFrameCaptureDir, ec);
    sFrameCaptureActive = true;
    sFrameCaptureCounter = 0;
    sFrameCaptureElapsed = 0.0f;
    sFrameCaptureDuration = dodge_duration + 0.3f;
    sFrameCaptureArmedNextDodge = false;
    std::fprintf(stderr, "[frame-capture] capturing dodge for %.2fs to %s/\n",
                 sFrameCaptureDuration, sFrameCaptureDir.c_str());
}

// Fire the dodge clip. WASD held → directional roll (falling_to_roll);
// no WASD → backstep (standing_dodge_backward). Both clips have
// monotonic hip XZ travel; rotated by sPlayer.yaw, the clip-local hip
// motion lands as world-frame travel. Sets all dodge state and arms
// any pending CSV/frame-capture recordings. Returns true on success.
static bool fireDodgeFromTap(const glm::vec3& moveIntent, float backstep_playback_rate,
                             float roll_playback_rate)
{
    const bool has_intent = glm::length(moveIntent) > 0.0001f;
    const char* clip_name = has_intent ? "falling_to_roll" : "standing_dodge_backward";
    sDodgeIsBackstep = !has_intent;
    if (has_intent)
    {
        const glm::vec3 dir = glm::normalize(moveIntent);
        sPlayer.yaw = yawFromGroundDir(dir);
    }
    const auto* dodgeClip = sClips.get(clip_name);
    if (dodgeClip == nullptr || !dodgeClip->isLoaded())
        return false;
    const float playback_rate = sDodgeIsBackstep ? backstep_playback_rate : roll_playback_rate;
    SpliceDiag diag;
    if (selva::combat::isCombatDebugEnabled())
        diag = captureSpliceDiag();
    fireOneShotWithProfile(*dodgeClip, profiles::dodge(), 0.0f, playback_rate, clip_name);
    if (selva::combat::isCombatDebugEnabled())
    {
        char prefix[256];
        std::snprintf(prefix, sizeof(prefix),
                      "[combat:dodge-fire %.4fs] clip=%s loco=%s "
                      "dur=%.3fs (rate=%.2f) ",
                      selva::wallClock(), clip_name, sLastLocoClipName.c_str(),
                      dodgeClip->duration() / playback_rate, playback_rate);
        logSpliceDiag(diag, *dodgeClip, 0.0f, prefix);
    }
    sDodgeActive = true;
    sDodgeElapsed = 0.0f;
    sDodgePlaybackRate = std::max(0.1f, playback_rate);
    sDodgeDuration = dodgeClip->duration() / sDodgePlaybackRate;
    sDodgeFiredThisPress = true;
    armDodgeCsvRecordingIfRequested(sDodgeDuration);
    armDodgeFrameCaptureIfRequested(sDodgeDuration);
    return true;
}

// Tick the Space input: tap-vs-hold disambiguation. Tap (release
// within dodge_tap_window) fires a dodge; hold past the window
// engages sprint; release always drops sprint. Returns true if a
// dodge fired (caller marks combat_input_this_frame).
// True when EITHER hand's cancel window currently includes wall-clock
// `now`. Used as the gate for cancel-into-dodge from a buffered press.
static bool eitherHandCancelWindowOpen()
{
    const float now = selva::wallClock();
    for (auto hand : {selva::combat::HandSide::Right, selva::combat::HandSide::Left})
    {
        const auto& w = selva::combat::cancelWindow(hand);
        if (w.close_at > w.open_at && now >= w.open_at && now <= w.close_at)
            return true;
    }
    return false;
}

// Fire a buffered dodge if its gate is open this frame. Gate (any
// of):
//   * No one-shot is active (clean handoff after attack/dodge ends).
//   * An attack's cancel window is open (cancel-into-dodge).
//   * Current dodge has passed dodge_cancel_fraction of its duration
//     (chain-roll: roll-into-roll near the standup tail).
// Buffer drops on fire OR on combo_input_buffer_seconds expiry.
static bool tryFireBufferedDodge(float backstep_playback_rate, float roll_playback_rate,
                                 float combo_input_buffer_seconds, float dodge_cancel_fraction)
{
    if (!sBufferedDodge.pending)
        return false;
    const float age = selva::wallClock() - sBufferedDodge.buffered_at;
    if (age > combo_input_buffer_seconds)
    {
        sBufferedDodge.pending = false;
        return false;
    }
    const bool dodge_cancel_open = sDodgeActive && sDodgeDuration > 0.0f &&
                                   (sDodgeElapsed / sDodgeDuration) >= dodge_cancel_fraction;
    const bool gate_open =
        !sSampler.isOneShotActive() || eitherHandCancelWindowOpen() || dodge_cancel_open;
    if (!gate_open)
        return false;
    const bool fired = fireDodgeFromTap(sBufferedDodge.move_intent_at_press, backstep_playback_rate,
                                        roll_playback_rate);
    sBufferedDodge.pending = false;
    if (fired)
        combatLog("[combat:rhythm %.4fs] buffered dodge FIRED (waited %.3fs since press, "
                  "via=%s)\n",
                  selva::wallClock(), age,
                  dodge_cancel_open ? "dodge-cancel"
                                    : (sSampler.isOneShotActive() ? "attack-cancel" : "clean"));
    return fired;
}

static bool tickSpaceInput(const Uint8* keys, const glm::vec3& moveIntent, float dt,
                           float dodge_tap_window, float backstep_playback_rate,
                           float roll_playback_rate)
{
    bool fired = false;
    const bool space_now = keys[SDL_SCANCODE_SPACE] != 0;
    const bool press_edge = space_now && !sPrevSpace;
    const bool release_edge = !space_now && sPrevSpace;
    if (press_edge)
    {
        sSpaceHeldSeconds = 0.0f;
        sDodgeFiredThisPress = false;
    }
    if (space_now)
    {
        sSpaceHeldSeconds += dt;
        if (sSpaceHeldSeconds >= dodge_tap_window)
            sPlayer.sprinting = true;
    }
    if (release_edge)
    {
        const bool was_tap = sSpaceHeldSeconds < dodge_tap_window;
        if (was_tap && !sDodgeFiredThisPress)
        {
            const bool blocked = sSampler.isOneShotActive() || sDodgeActive;
            if (!blocked)
            {
                fired = fireDodgeFromTap(moveIntent, backstep_playback_rate, roll_playback_rate);
            }
            else
            {
                // Buffer the dodge; per-frame fire path picks it up
                // when the active hand's cancel window opens, when
                // the current dodge passes its cancel fraction, or
                // when the active one-shot fully ends.
                sBufferedDodge.pending = true;
                sBufferedDodge.move_intent_at_press = moveIntent;
                sBufferedDodge.buffered_at = selva::wallClock();
                combatLog("[combat:rhythm %.4fs] dodge BUFFERED (%s in flight)\n",
                          selva::wallClock(), sDodgeActive ? "dodge" : "one-shot");
            }
        }
        sPlayer.sprinting = false;
    }
    sPrevSpace = space_now;
    return fired;
}

// Pre-update joint snapshot used by the splice-residual diagnostic.
struct PreUpdateJoint
{
    const char* name;
    int idx;
    glm::vec3 live;
};

// Debounce sStableWasdIntent against the raw WASD bit. Held WASD
// commits to walking only after disagreement persists for the
// debounce window; brief taps + rapid mashing never accumulate.
static void tickWasdDebounce(bool wasd_intent_raw, float wasd_debounce_seconds)
{
    if (wasd_intent_raw == sStableWasdIntent)
    {
        sWasdDisagreeStartedAt = -1.0f;
        return;
    }
    if (sWasdDisagreeStartedAt < 0.0f)
    {
        sWasdDisagreeStartedAt = selva::wallClock();
        return;
    }
    if (selva::wallClock() - sWasdDisagreeStartedAt >= wasd_debounce_seconds)
    {
        sStableWasdIntent = wasd_intent_raw;
        sWasdDisagreeStartedAt = -1.0f;
    }
}

// Edge-trace SM-input changes: WASD intent, attack-in-flight, recovery
// gate, is-moving. Lets us correlate input edges to SM decisions.
static void logSmInputChanges(bool wasd_intent, bool attack_in_flight, bool in_attack_recovery,
                              bool is_moving)
{
    if (!selva::combat::isCombatDebugEnabled())
        return;
    static bool prev_wasd_intent = false;
    static bool prev_attack_in_flight = false;
    static bool prev_in_recovery = false;
    static bool prev_is_moving = false;
    if (wasd_intent != prev_wasd_intent || attack_in_flight != prev_attack_in_flight ||
        in_attack_recovery != prev_in_recovery || is_moving != prev_is_moving)
    {
        const auto fd = sSampler.frameDiagnostics();
        combatLog("[sm %.4fs] wasd=%d att=%d rec=%d -> is_moving=%d  "
                  "(lockout_until=%.3fs, one_shot_w=%.2f, loco_w=%.2f)\n",
                  selva::wallClock(), wasd_intent ? 1 : 0, attack_in_flight ? 1 : 0,
                  in_attack_recovery ? 1 : 0, is_moving ? 1 : 0, selva::combat::locoLockoutUntil(),
                  fd.one_shot_weight, fd.loco_blend_weight);
        prev_wasd_intent = wasd_intent;
        prev_attack_in_flight = attack_in_flight;
        prev_in_recovery = in_attack_recovery;
        prev_is_moving = is_moving;
    }
}

// Capture pre-update live joint world positions for the residual log.
// The actual splice point the sampler picked is only known AFTER
// update runs.
static std::vector<PreUpdateJoint>
capturePreUpdateJoints(bool loco_clip_changed, const std::string& clip_name, float blend_seconds)
{
    std::vector<PreUpdateJoint> out;
    if (!selva::combat::isCombatDebugEnabled() || !loco_clip_changed)
        return out;
    const auto fd = sSampler.frameDiagnostics();
    combatLog("[sm %.4fs] loco-pick %s@%.3fs -> %s (blend=%.3fs)\n", selva::wallClock(),
              sLastLocoClipName.c_str(), fd.loco_current_time, clip_name.c_str(), blend_seconds);
    const char* names[] = {"mixamorig:RightHand", "mixamorig:LeftHand", "mixamorig:RightFoot",
                           "mixamorig:LeftFoot", "mixamorig:Hips"};
    for (const char* n : names)
    {
        const int idx = sSampler.findJoint(n);
        if (idx < 0)
            continue;
        out.push_back({n, idx, sSampler.jointWorldPos(idx)});
    }
    return out;
}

// Result of locomotion clip selection for this frame.
struct LocomotionPick
{
    std::string clip_name;
    const selva::anim::AnimationClip* clip = nullptr;
    bool loops = true;
    float blend_seconds = 0.0f;
    std::vector<PreUpdateJoint> pre_update_joints;
};

// Bump blend_seconds when the source and destination clips belong
// to different families (per locomotion.json `family` tag). The
// pose gap across families is geometrically wider than within;
// the default 0.10-0.20s blend produces 0.3-0.8m foot/hand
// residuals at the splice (visible as leg jerk / arm snap).
// Wider blend gives the residual more time to ease out — the
// inertialization decay matches blend_seconds, so the offset gets
// a longer window to absorb on top of the wider crossfade.
//
// Either side untagged (empty family) → no bump (treat as
// "matches everything"). This makes adding a new clip safe-by-
// default: forget the family tag and you get default blends, no
// regression.
//
// Tunable via F1 panel "Cross-family min (s)".
static float extendBlendForCrossFamily(const std::string& from, const std::string& to,
                                       float current_blend, float cross_family_min)
{
    const std::string& from_family = sLocomotionConfig.family(from);
    const std::string& to_family = sLocomotionConfig.family(to);
    if (from_family.empty() || to_family.empty() || from_family == to_family)
        return current_blend;
    return std::max(current_blend, cross_family_min);
}

// Pick the locomotion clip for this frame. Honors the F1 debug
// clip-preview override; otherwise drives the locomotion SM with
// debounced WASD and applies the held-block + loco-freeze-during-
// BlendIn overrides. Updates sLastLocoClipName for next-frame combat
// diagnostics.
static LocomotionPick selectLocomotionClip(const glm::vec3& moveIntent, float dt,
                                           bool combat_input_this_frame,
                                           const selva::tuning::Tunables& tun)
{
    LocomotionPick pick;
    pick.blend_seconds = tun.anim_blend_seconds;
    if (!sDebugClipName.empty())
    {
        pick.clip_name = sDebugClipName;
        pick.clip = sClips.get(pick.clip_name);
        pick.loops = true;
        pick.blend_seconds =
            sLocomotionConfig.blendInSeconds(pick.clip_name, tun.anim_blend_seconds);
        if (pick.clip != nullptr)
            return pick;
    }

    const bool wasd_intent_raw = glm::length(moveIntent) > 0.0001f;
    tickWasdDebounce(wasd_intent_raw, tun.wasd_debounce_seconds);
    const bool wasd_intent = sStableWasdIntent;
    const bool attack_in_flight = sSampler.isOneShotActive() || sPendingFirstAction.active;
    const bool in_attack_recovery = selva::wallClock() < selva::combat::locoLockoutUntil();
    const bool is_moving = wasd_intent;
    const bool is_sprinting = sPlayer.sprinting && wasd_intent;
    logSmInputChanges(wasd_intent, attack_in_flight, in_attack_recovery, is_moving);

    const bool clip_done_this_frame = sSampler.locomotionClipFinished();
    const bool is_armed = !isUnarmed(sEquipment);
    selva::gameplay::LocomotionTickInput sm_in;
    sm_in.is_moving = is_moving;
    sm_in.is_sprinting = is_sprinting;
    sm_in.clip_finished_this_frame = clip_done_this_frame;
    sm_in.combat_input_this_frame = combat_input_this_frame;
    sm_in.is_armed = is_armed;
    sm_in.dt = dt;
    sm_in.combat_grace_seconds = tun.combat_idle_grace_seconds;
    sm_in.wall_clock_seconds = selva::wallClock();
    const auto sm_out = tickLocomotionStateMachine(sLocomotionSM, sm_in);
    pick.clip_name = sm_out.clip_name;
    pick.loops = sm_out.loops;
    pick.blend_seconds = sm_out.blend_seconds;

    // Held-block override: force loco track to the block-idle loop
    // when blocking with a buckler (which has a dedicated idle clip).
    if (sBlockingActive && offHandCanBlock(sEquipment))
        pick.clip_name = "sword_and_shield_block_idle";

    // Loco-freeze: hold the previous loco clip when a one-shot is
    // staged-for-handoff and a swap now would corrupt the BlendOut
    // pose-match. Two conditions:
    //   * Phase=BlendIn: loco + one-shot both partially visible →
    //     multi-source collision (loco ramp + new loco splice + one-
    //     shot ramp) reads as a leg spasm.
    //   * Phase=Hold + full-mask: loco fully occluded, but a swap
    //     here stages a geometrically incompatible clip for the
    //     upcoming BlendOut pose-match (e.g. dodge exit pose vs
    //     running's t=0 produces 0.8m+ residuals). PoseSampler also
    //     enforces this gate internally; we mirror it here so
    //     sLastLocoClipName / pre-update joint capture / residual
    //     log don't desync against the suppressed swap.
    const auto fd_loco = sSampler.frameDiagnostics();
    const bool one_shot_blending_in = fd_loco.one_shot_phase == 1;
    const bool loco_frozen = sSampler.isLocoFrozenByOneShot();
    if ((one_shot_blending_in || loco_frozen) && !sLastLocoClipName.empty() &&
        pick.clip_name != sLastLocoClipName)
        pick.clip_name = sLastLocoClipName;

    pick.clip = sClips.get(pick.clip_name);
    const bool loco_clip_changed = (pick.clip_name != sLastLocoClipName);
    const std::string from_clip = sLastLocoClipName;
    pick.blend_seconds = sLocomotionConfig.blendInSeconds(pick.clip_name, pick.blend_seconds);
    pick.blend_seconds = extendBlendForCrossFamily(from_clip, pick.clip_name, pick.blend_seconds,
                                                   tun.cross_family_min_blend_seconds);
    // Capture pre-update joints AND emit the [sm loco-pick] log after
    // the final blend has been computed, so the log shows the
    // duration the sampler will actually use (not the SM's pre-
    // override value).
    pick.pre_update_joints =
        capturePreUpdateJoints(loco_clip_changed, pick.clip_name, pick.blend_seconds);
    sLastLocoClipName = pick.clip_name;
    return pick;
}

// Post-update splice-residual diagnostic. Sample the new clip at the
// splice time the sampler picked and log the per-joint residual vs
// pre-update live pose.
// Per-joint residual thresholds for anomaly tagging. A residual
// over the threshold prefixes the line with `[!]` so the eye
// catches it when scanning the log. Calibration target: empirically
// "smooth" walking<->running splices land feet at 0.16-0.22m mid-
// stride and hands within 0.20m; visible spasms hit feet 0.4m+ and
// hands 0.4m+. Thresholds chosen to be just above the smooth
// upper-bound so within-family gait transitions don't generate
// noise but cross-family (combat-idle <-> gait) and BlendOut-
// handoff anomalies still flag.
static float anomalyThresholdForJoint(const char* joint_name)
{
    if (std::strstr(joint_name, "Hand") != nullptr)
        return 0.30f;
    if (std::strstr(joint_name, "Foot") != nullptr)
        return 0.30f;
    if (std::strstr(joint_name, "Hips") != nullptr)
        return 0.15f;
    return 0.30f;
}

static void logSpliceResiduals(const std::vector<PreUpdateJoint>& pre_update_joints,
                               const selva::anim::AnimationClip* clip)
{
    if (pre_update_joints.empty() || clip == nullptr)
        return;
    const auto fd = sSampler.frameDiagnostics();
    const float splice_t = fd.loco_current_time;
    bool any_anomaly = false;
    for (const auto& j : pre_update_joints)
    {
        const glm::vec3 cand = sSampler.sampleJointWorldPos(*clip, splice_t, j.idx);
        const float residual = glm::length(cand - j.live);
        const float threshold = anomalyThresholdForJoint(j.name);
        const bool anomaly = residual > threshold;
        if (anomaly)
            any_anomaly = true;
        combatLog("  %s%s residual=|%.3fm|%s live=(%.2f,%.2f,%.2f) splice_t=%.3fs "
                  "cand=(%.2f,%.2f,%.2f)\n",
                  anomaly ? "[!] " : "    ", j.name, residual, anomaly ? " (>threshold)" : "",
                  j.live.x, j.live.y, j.live.z, splice_t, cand.x, cand.y, cand.z);
    }
    if (any_anomaly)
    {
        // Echo the active state on an anomaly so the cause is right
        // there without having to scroll up: which one-shot, which
        // loco, which phase.
        combatLog("  [!] context: 1shot=%s(phase=%d) loco=%s->%s blend_w=%.2f\n",
                  fd.one_shot_name ? fd.one_shot_name : "(none)", fd.one_shot_phase,
                  fd.loco_previous_name ? fd.loco_previous_name : "(none)",
                  fd.loco_current_name ? fd.loco_current_name : "(none)", fd.loco_blend_weight);
    }
}

// During one-shot BlendOut (phase=3), emit per-frame hip + foot
// world positions so we can spot leg/hip jumps as the loco track
// becomes visible. Anomaly diagnostic, not a generic state dump
// (logStateNarrative covers the discrete state).
static void logBlendOutFadeJoints()
{
    if (!selva::combat::isCombatDebugEnabled())
        return;
    const auto fd = sSampler.frameDiagnostics();
    if (fd.one_shot_phase != 3)
        return;
    for (const char* n : {"mixamorig:Hips", "mixamorig:LeftFoot", "mixamorig:RightFoot"})
    {
        const int idx = sSampler.findJoint(n);
        if (idx < 0)
            continue;
        const glm::vec3 p = sSampler.jointWorldPos(idx);
        combatLog("    [fade %.4fs] %s=(%.2f,%.2f,%.2f) one_shot_w=%.2f\n", selva::wallClock(), n,
                  p.x, p.y, p.z, fd.one_shot_weight);
    }
}

// Rotate clip-local hip XZ delta into world frame using the player's
// yaw and add it to sPlayer.pos. Y is preserved (clip authors no
// vertical motion in the locomotion path; freeze handles the rest).
static void applyHipDeltaToPlayerPos()
{
    const glm::vec3 hip_local = sSampler.consumedHipDelta();
    if (glm::length(glm::vec2(hip_local.x, hip_local.z)) <= 1e-6f)
        return;
    const float sy = std::sin(sPlayer.yaw);
    const float cy = std::cos(sPlayer.yaw);
    const glm::vec3 hip_world(-cy * hip_local.x - sy * hip_local.z, 0.0f,
                              sy * hip_local.x - cy * hip_local.z);
    sPlayer.pos += hip_world;
}

// Mid-roll yaw steer: bend the trajectory toward the live move-intent
// at dodge_steer_rate per second. Backsteps don't steer (the
// defensive beat commits to its initial axis).
static void applyDodgeSteer(const glm::vec3& moveIntent, float dodge_steer_rate, float dt)
{
    if (sDodgeIsBackstep || glm::length(moveIntent) <= 0.0001f)
        return;
    const glm::vec3 dir = glm::normalize(moveIntent);
    const float targetYaw = yawFromGroundDir(dir);
    float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
    const float maxStep = dodge_steer_rate * dt;
    if (delta > maxStep)
        delta = maxStep;
    else if (delta < -maxStep)
        delta = -maxStep;
    sPlayer.yaw += delta;
}

// Flush an in-progress CSV bone-trajectory recording to disk and clear
// the recording state.
static void flushCsvRecording()
{
    const std::string out_path = "debug_bones_game_" + sDebugRecordClipName + ".csv";
    std::FILE* fp = std::fopen(out_path.c_str(), "w");
    if (fp == nullptr)
    {
        std::fprintf(stderr, "[anim-debug] failed to open %s for write\n", out_path.c_str());
        sDebugRecording = false;
        sDebugRecordSamples.clear();
        return;
    }
    std::fprintf(fp, "time_s,joint_name,x,y,z\n");
    const int nj = sSampler.jointCount();
    for (const auto& s : sDebugRecordSamples)
    {
        for (int i = 0; i < nj && i < static_cast<int>(s.joints.size()); ++i)
        {
            std::fprintf(fp, "%.4f,%s,%.6f,%.6f,%.6f\n", s.time_s, sSampler.jointName(i),
                         s.joints[i].x, s.joints[i].y, s.joints[i].z);
        }
    }
    std::fclose(fp);
    std::fprintf(stderr, "[anim-debug] wrote %s (%zu frames)\n", out_path.c_str(),
                 sDebugRecordSamples.size());
    sDebugRecording = false;
    sDebugRecordSamples.clear();
}

// Tick the CSV bone-trajectory recorder: append this frame's joint
// world positions; when duration elapses, flush to disk.
static void tickCsvRecording(float dt)
{
    if (!sDebugRecording)
        return;
    DebugBoneSample sample;
    sample.time_s = sDebugRecordElapsed;
    const int n = sSampler.jointCount();
    sample.joints.reserve(n);
    for (int i = 0; i < n; ++i)
        sample.joints.push_back(sSampler.jointWorldPos(i));
    sDebugRecordSamples.push_back(std::move(sample));
    sDebugRecordElapsed += dt;
    if (sDebugRecordElapsed >= sDebugRecordDuration)
        flushCsvRecording();
}

// Compute the camera-relative WASD movement intent vector. Forward
// is -Z rotated by camera yaw; right is perpendicular in XZ.
static glm::vec3 computeMoveIntent(const Uint8* keys)
{
    const glm::vec3 camFwd(-std::sin(selva::render::cameraYaw()), 0.0f,
                           -std::cos(selva::render::cameraYaw()));
    const glm::vec3 camRight(std::cos(selva::render::cameraYaw()), 0.0f,
                             -std::sin(selva::render::cameraYaw()));
    glm::vec3 moveIntent(0.0f);
    if (keys[SDL_SCANCODE_W])
        moveIntent += camFwd;
    if (keys[SDL_SCANCODE_S])
        moveIntent -= camFwd;
    if (keys[SDL_SCANCODE_D])
        moveIntent += camRight;
    if (keys[SDL_SCANCODE_A])
        moveIntent -= camRight;
    return moveIntent;
}

// Bundle of LMB/RMB/F edge bits for the per-frame combat-input pass.
struct CombatInputEdges
{
    bool lmb_now;
    bool rmb_now;
    bool press_lmb;
    bool press_rmb;
    bool release_lmb;
    bool release_rmb;
    bool press_f;
    bool shift_held;
};

// Read raw mouse + Shift + F state and compute the per-frame edges.
// `tryAttackInput` and friends consume the result.
static CombatInputEdges readCombatInputEdges(const Uint8* keys)
{
    const Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    CombatInputEdges e;
    e.lmb_now = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    e.rmb_now = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    e.press_lmb = e.lmb_now && !sPrevLMB;
    e.press_rmb = e.rmb_now && !sPrevRMB;
    e.release_lmb = !e.lmb_now && sPrevLMB;
    e.release_rmb = !e.rmb_now && sPrevRMB;
    const bool fNow = keys[SDL_SCANCODE_F] != 0;
    e.press_f = fNow && !sPrevF;
    e.shift_held = (keys[SDL_SCANCODE_LSHIFT] != 0) || (keys[SDL_SCANCODE_RSHIFT] != 0);
    return e;
}

// Run the per-frame combat-input pass: edge log, stance toggle, LMB
// → attack, RMB → block-or-attack, pending block-latch commit.
// Returns true if anything fired this frame.
static bool tickCombatInputPass(const Uint8* keys, const CombatInputEdges& e,
                                const selva::tuning::Tunables& tun)
{
    bool fired = false;
    logInputEdges(keys, e.press_lmb, e.release_lmb, e.press_rmb, e.release_rmb);
    if (tryHandleStanceTogglePress(e.press_f, e.shift_held))
        fired = true;

    const selva::combat::PressModifiers mods{e.shift_held, sPlayer.sprinting};
    if (tryAttackInputDispatch(selva::combat::HandSide::Right, e.press_lmb, "LMB", mods,
                               tun.combo_input_buffer_seconds))
        fired = true;

    bool block_freeze_last = false;
    const char* block_clip_name = resolveBlockClip(e.shift_held, block_freeze_last);
    if (block_clip_name != nullptr)
    {
        if (tickBlockingFromRMB(block_clip_name, block_freeze_last, e.press_rmb, e.release_rmb,
                                tun.combat_entry_delay_seconds))
            fired = true;
    }
    else
    {
        if (tryAttackInputDispatch(selva::combat::HandSide::Right, e.press_rmb, "RMB", mods,
                                   tun.combo_input_buffer_seconds))
            fired = true;
    }
    if (tickPendingBlockLatch())
        fired = true;
    return fired;
}

// Tick the dodge-roll timer; once elapsed reaches duration, clear the
// active flag (the one-shot blend-out continues briefly after).
static void tickDodgeTimer(float dt)
{
    if (!sDodgeActive)
        return;
    sDodgeElapsed += dt;
    if (sDodgeElapsed >= sDodgeDuration)
    {
        sDodgeActive = false;
        if (selva::combat::isCombatDebugEnabled())
            combatLog("[combat:dodge %.4fs] sDodgeActive=false (elapsed=%.3fs/%.3fs)\n",
                      selva::wallClock(), sDodgeElapsed, sDodgeDuration);
    }
}

// Rising-edge Y/G toggles grip mode (one-handed ↔ two-handed).
static void tickGripToggle(const Uint8* keys)
{
    const bool gripNow = (keys[SDL_SCANCODE_Y] != 0) || (keys[SDL_SCANCODE_G] != 0);
    if (gripNow && !sPrevGripToggle)
    {
        sEquipment.grip = (sEquipment.grip == selva::combat::Grip::TwoHanded)
                              ? selva::combat::Grip::OneHanded
                              : selva::combat::Grip::TwoHanded;
        std::fprintf(stderr, "[combat] grip → %s\n",
                     sEquipment.grip == selva::combat::Grip::TwoHanded ? "two_handed"
                                                                       : "one_handed");
    }
    sPrevGripToggle = gripNow;
}

// Build a state-signature string that captures the discrete state.
// Used to dedup per-frame emissions: only emit when this changes.
// Times are deliberately excluded — they change every tick.
static std::string buildStateSignature(const selva::anim::PoseSampler::FrameDiagnostics& fd)
{
    char sig[256];
    std::snprintf(sig, sizeof(sig), "p=%d|os=%s|loco=%s|prev=%s|d=%d|b=%d|bda=%d|fr=%d",
                  fd.one_shot_phase, fd.one_shot_name ? fd.one_shot_name : "",
                  fd.loco_current_name ? fd.loco_current_name : "",
                  fd.loco_previous_name ? fd.loco_previous_name : "", sDodgeActive ? 1 : 0,
                  sBufferedDodge.pending ? 1 : 0, sPostDodgeAttack.pending ? 1 : 0,
                  sSampler.isLocoFrozenByOneShot() ? 1 : 0);
    return sig;
}

// Append the dodge / one-shot / loco / freeze / buffer sections to
// the state-narrative line. Sections that have nothing interesting
// say nothing.
static void appendDodgeSection(std::string& line)
{
    if (!sDodgeActive)
        return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "dodge=active(%.2f/%.2fs) ", sDodgeElapsed, sDodgeDuration);
    line += buf;
}

static void appendOneShotSection(std::string& line,
                                 const selva::anim::PoseSampler::FrameDiagnostics& fd)
{
    if (fd.one_shot_phase == 0 || fd.one_shot_name == nullptr)
        return;
    const char* phase_name = (fd.one_shot_phase == 1)   ? "BlendIn"
                             : (fd.one_shot_phase == 2) ? "Hold"
                             : (fd.one_shot_phase == 3) ? "BlendOut"
                                                        : "?";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "1shot=%s(%s,t=%.2f) ", fd.one_shot_name, phase_name,
                  fd.one_shot_time);
    line += buf;
}

static void appendLocoSection(std::string& line,
                              const selva::anim::PoseSampler::FrameDiagnostics& fd)
{
    if (fd.loco_current_name == nullptr)
        return;
    char buf[128];
    if (fd.loco_previous_name != nullptr && fd.loco_blend_duration > 0.0f)
    {
        std::snprintf(buf, sizeof(buf), "loco=%s@%.2fs<-%s w=%.2f ", fd.loco_current_name,
                      fd.loco_current_time, fd.loco_previous_name, fd.loco_blend_weight);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "loco=%s@%.2fs ", fd.loco_current_name,
                      fd.loco_current_time);
    }
    line += buf;
}

static void appendBufferSection(std::string& line, float now)
{
    char buf[64];
    if (sBufferedDodge.pending)
    {
        std::snprintf(buf, sizeof(buf), "buf=dodge(%.2fs) ", now - sBufferedDodge.buffered_at);
        line += buf;
    }
    if (sPostDodgeAttack.pending)
    {
        std::snprintf(buf, sizeof(buf), "buf=postdodge-%s(%.2fs) ", sPostDodgeAttack.button,
                      now - sPostDodgeAttack.buffered_at);
        line += buf;
    }
}

// Single-line state-narrative summary. Throttled: emits only when
// the discrete state diff has changed since the last call. Reads as
// a sequence of state transitions, not a tick stream.
//
// Format example:
//   [state 12.345s] dodge=active(0.43/0.87s) 1shot=falling_to_roll(Hold,t=0.55)
//                   loco=walking@0.34s<-running w=0.62 FROZEN buf=dodge(0.12s)
static void logStateNarrative()
{
    if (!selva::combat::isCombatDebugEnabled())
        return;
    const auto fd = sSampler.frameDiagnostics();
    const std::string sig = buildStateSignature(fd);
    static std::string sLastSig;
    if (sLastSig == sig)
        return;
    sLastSig = sig;

    const float now = selva::wallClock();
    char header[64];
    std::snprintf(header, sizeof(header), "[state %.4fs] ", now);
    std::string line = header;
    appendDodgeSection(line);
    appendOneShotSection(line, fd);
    appendLocoSection(line, fd);
    if (sSampler.isLocoFrozenByOneShot())
        line += "FROZEN ";
    appendBufferSection(line, now);
    line += "\n";
    combatLog("%s", line.c_str());
}

static void selvaPerFrame(Engine& engine, EntityManager& /*em*/, double dt_d)
{
    ZoneScopedN("selvaPerFrame");
    // Apply the global time-scale multiplier ONCE here. All downstream
    // dt-driven systems (sampler.update, dodge timer, selva::wallClock(),
    // lockouts, debounce, etc.) read this scaled dt. Set time_scale
    // < 1.0 in the F1 panel to slow-mo every animation transition for
    // debugging split-second issues.
    const float dt = static_cast<float>(dt_d) * selva::tuning::current().time_scale;

    bool combat_input_this_frame = false;

    selva::advanceWallClock(dt);

    // Title bar — game name + FPS. EMA-smoothed so the number doesn't flicker.
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Selva Oscura  |  FPS " + std::to_string(fps));

    const auto& tun = selva::tuning::current();
    // Re-apply inertialization decay shape every frame so F1 panel
    // edits to base / scale / max take effect live without a
    // restart. Cheap (3 float assignments inside the sampler).
    sSampler.setInertializationScaling(tun.inertialize_decay_base_seconds,
                                       tun.inertialize_decay_scale_per_radian,
                                       tun.inertialize_decay_max_seconds);
    tickMouseLook(tun);

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

    tickF1TuningPanelToggle(keys);

    // -------- Combat input --------
    //   LMB / RMB        — right-hand attacks (chain shared)
    //   Shift+LMB/RMB    — heavy
    //   Sprint+LMB/RMB   — running attack
    //   F                — combat-stance toggle (Shift+F flips foot)
    //   Y / G            — grip toggle
    //   Space tap        — dodge; hold past tap window → sprint
    //
    // Tuning-panel-open suppresses combat-relevant clicks (mouse drag
    // on sliders shouldn't trigger swings).
    const CombatInputEdges edges = readCombatInputEdges(keys);
    selva::combat::tickChainExpiry(selva::wallClock(), tun.combo_input_buffer_seconds);
    selva::combat::tickChainObserver(selva::wallClock(), sSampler.isOneShotActive());
    if (!sShowTuningPanel && tickCombatInputPass(keys, edges, tun))
        combat_input_this_frame = true;
    sPrevLMB = edges.lmb_now;
    sPrevRMB = edges.rmb_now;
    sPrevF = (keys[SDL_SCANCODE_F] != 0);

    selva::combat::cacheRightHandPos(sSampler);
    tickGripToggle(keys);

    const glm::vec3 moveIntent = computeMoveIntent(keys);

    if (tickSpaceInput(keys, moveIntent, dt, tun.dodge_tap_window, tun.backstep_playback_rate,
                       tun.roll_playback_rate))
        combat_input_this_frame = true;

    // Tick the dodge timer. The active gate ends at the clip's full
    // duration. With root motion, the clip's authored hip motion goes
    // to ~zero during the standup tail, so additional translation
    // naturally tapers — there's no "post-landing slide" that the
    // legacy script-push had, which is why we no longer need a
    // separate motion_end cutoff.
    tickDodgeTimer(dt);
    logOneShotStateTransitions();
    if (tryFirePostDodgeAttack(edges.shift_held, tun.combo_input_buffer_seconds))
        combat_input_this_frame = true;
    if (tryFireBufferedDodge(tun.backstep_playback_rate, tun.roll_playback_rate,
                             tun.combo_input_buffer_seconds, tun.dodge_cancel_fraction))
        combat_input_this_frame = true;

    // Frame-capture timer (independent of dodge so post-dodge recovery
    // can also be captured). On expiry: emit contact-sheet composite.
    tickFrameCapture(dt);

    // Every active attack one-shot locks movement until it blends out.
    // The whole body is committed to the clip, so letting WASD push the
    // player would compound translation against the attack's authored
    // hip drive. WASD held during the lock is honored as soon as the
    // one-shot ends — the SM picks `walking` (combat-idle if not moving)
    // automatically because `is_moving` is read live each frame.
    const bool one_shot_active = sSampler.isOneShotActive();
    const bool attack_locks = true;
    // Dodge always locks movement — the dodge code drives sPlayer.pos
    // along sDodgeDir for the clip's duration. Letting WASD also push
    // the player would compound translation again (same pattern as the
    // root-motion problem we already fixed for locomotion).
    const bool movement_locked = sDodgeActive || (one_shot_active && attack_locks);
    // Yaw is gameplay-driven (the clip authors hip motion in clip-local
    // space; we rotate that delta by sPlayer.yaw to land it in world
    // frame). Smooth-turn toward the camera-relative move intent at
    // tun.turn_rate. Translation itself happens after sSampler.update
    // below, where we read consumedHipDelta from the freshly-sampled
    // pose.
    tickPlayerYaw(moveIntent, movement_locked, tun.turn_rate, dt);

    // Pick + advance the sampler. The locomotion SM picks the loco
    // clip; F1 debug-clip preview overrides it. sampler.update runs
    // last so it sees post-input, post-movement state.
    const LocomotionPick pick = selectLocomotionClip(moveIntent, dt, combat_input_this_frame, tun);
    if (pick.clip != nullptr && pick.clip->isLoaded())
    {
        ZoneScopedN("sampler.update");
        sSampler.update(*pick.clip, dt, pick.blend_seconds, pick.loops, pick.clip_name.c_str());
    }

    logSpliceResiduals(pick.pre_update_joints, pick.clip);
    logBlendOutFadeJoints();

    // Root-motion translation. Locomotion path runs unless we're
    // dodging or in F1 clip-preview mode (the dodge block below owns
    // world push during a dodge so we don't double-translate).
    if (!sDodgeActive && sDebugClipName.empty())
        applyHipDeltaToPlayerPos();

    // Dodge translation: clip's authored hip motion drives world
    // translation directly (rolls = clip-local +Z forward, backsteps =
    // clip-local -Z backward). Mid-roll steering bends the trajectory.
    if (sDodgeActive && sDebugClipName.empty())
    {
        applyDodgeSteer(moveIntent, tun.dodge_steer_rate, dt);
        applyHipDeltaToPlayerPos();
    }

    tickCsvRecording(dt);
    logStateNarrative();
}

static void selvaRenderWorld(Engine& /*engine*/, EntityManager& /*em*/, float /*camX*/,
                             float /*camY*/, float /*alpha*/)
{
    ZoneScopedN("selvaRenderWorld");
    // LookAt height tracks the character's hip Y so dynamic poses
    // (rolls, knockdowns) keep the body in frame. Smoothed in
    // buildViewProj to avoid camera-shake during fast Y excursions.
    float targetLookAtY = 1.3f;
    if (sSampler.jointCount() > 0)
    {
        for (int i = 0; i < sSampler.jointCount(); ++i)
        {
            if (std::strcmp(sSampler.jointName(i), "mixamorig:Hips") == 0)
            {
                targetLookAtY = sSampler.jointWorldPos(i).y + 0.3f;
                break;
            }
        }
    }
    const glm::mat4 viewProj = selva::render::buildViewProj(sPlayer.pos, targetLookAtY);

    selva::render::useSceneProgram();
    selva::render::setSceneViewProj(viewProj);
    selva::render::renderEnvironment();
    glBindVertexArray(0);
    glUseProgram(0);

    // 3. Player — drawn as the rigged X Bot mesh, animated by the
    //    PoseSampler. Position comes from sPlayer.pos (X/Z); Y is
    //    -foot_offset_y so feet land on the floor regardless of where
    //    the rig's origin sits in bind pose. Yaw rotates the model
    //    around world-up to face the player's heading.
    if (sPlayerMesh.isLoaded() && !sSampler.bone_palette.empty())
    {
        const glm::vec3 player_pos(sPlayer.pos.x, -sPlayerMesh.foot_offset_y, sPlayer.pos.z);
        glm::mat4 player_model = glm::translate(glm::mat4(1.0f), player_pos);
        // Mixamo characters bind facing +Z. Our gameplay convention is
        // "yaw=0 means facing -Z" (matches camera-forward maths in
        // movement code). Add a 180° offset around world-up so
        // gameplay yaw 0 visually faces -Z while still lining up with
        // the bind-pose rig.
        player_model =
            glm::rotate(player_model, sPlayer.yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        selva::anim::drawSkeletalMesh(sPlayerMesh, player_model, viewProj, sSampler.bone_palette,
                                      1.0f);
    }

    // Frame capture: read the back buffer at quarter-resolution and
    // write a PNG. Done at the end of selvaRenderWorld — before ImGui
    // overlays — so captured images are pure gameplay view.
    //
    // Quarter-res because PNG-encoding the full back buffer (~2K) blocks
    // the render thread for ~100ms per frame, dropping the capture rate
    // from 60Hz to ~10Hz and yielding only ~7 frames over a 2.4s dodge.
    // At 1/4 size the encode is ~16x cheaper and the game stays at full
    // frame rate during capture.
    if (sFrameCaptureActive)
    {
        ZoneScopedN("frame-capture-write");
        constexpr int kCaptureScale = 4;
        const int src_w = selva::render::windowWidth();
        const int src_h = selva::render::windowHeight();
        const int dst_w = src_w / kCaptureScale;
        const int dst_h = src_h / kCaptureScale;
        if (src_w > 0 && src_h > 0 && dst_w > 0 && dst_h > 0)
        {
            std::vector<unsigned char> pixels(static_cast<std::size_t>(src_w) * src_h * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, src_w, src_h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            // Stride-sample down (cheap) and flip vertically (glReadPixels
            // is bottom-up, PNG is top-down).
            std::vector<unsigned char> small(static_cast<std::size_t>(dst_w) * dst_h * 3);
            for (int y = 0; y < dst_h; ++y)
            {
                const int src_y = y * kCaptureScale;
                const int dst_row_off = (dst_h - 1 - y) * dst_w * 3;
                for (int x = 0; x < dst_w; ++x)
                {
                    const int src_off = (src_y * src_w + x * kCaptureScale) * 3;
                    const int dst_off = dst_row_off + x * 3;
                    small[dst_off + 0] = pixels[src_off + 0];
                    small[dst_off + 1] = pixels[src_off + 1];
                    small[dst_off + 2] = pixels[src_off + 2];
                }
            }
            char path[512];
            std::snprintf(path, sizeof(path), "%s/frame_%04d.png", sFrameCaptureDir.c_str(),
                          sFrameCaptureCounter);
            stbi_write_png(path, dst_w, dst_h, 3, small.data(), dst_w * 3);
            ++sFrameCaptureCounter;
        }
    }
}

// tickstate:: accessors � TuningPanel uses these to read/write the
// F1-armed flags and debug-clip-preview state without poking the
// file-scope statics directly.
namespace selva::gameplay::tickstate
{
bool& showTuningPanel()
{
    return ::sShowTuningPanel;
}
std::string& debugClipName()
{
    return ::sDebugClipName;
}
bool& debugLoop()
{
    return ::sDebugLoop;
}
bool& debugRecording()
{
    return ::sDebugRecording;
}
float& debugRecordElapsed()
{
    return ::sDebugRecordElapsed;
}
float& debugRecordDuration()
{
    return ::sDebugRecordDuration;
}
std::string& debugRecordClipName()
{
    return ::sDebugRecordClipName;
}
bool& debugRecordArmedNextDodge()
{
    return ::sDebugRecordArmedNextDodge;
}
bool& frameCaptureArmedNextDodge()
{
    return ::sFrameCaptureArmedNextDodge;
}
bool& frameCaptureArmedNextChain()
{
    return ::sFrameCaptureArmedNextChain;
}
bool frameCaptureActive()
{
    return ::sFrameCaptureActive;
}
float frameCaptureElapsed()
{
    return ::sFrameCaptureElapsed;
}
float frameCaptureDuration()
{
    return ::sFrameCaptureDuration;
}
int frameCaptureCounter()
{
    return ::sFrameCaptureCounter;
}
std::size_t debugRecordSampleCount()
{
    return ::sDebugRecordSamples.size();
}
void resetDebugRecordSamples(std::size_t expected)
{
    ::sDebugRecordSamples.clear();
    ::sDebugRecordSamples.reserve(expected);
}
} // namespace selva::gameplay::tickstate

namespace selva::gameplay
{
void selvaPerFrame(::Engine& engine, ::EntityManager& em, double dt)
{
    ::selvaPerFrame(engine, em, dt);
}
void selvaRenderWorld(::Engine& engine, ::EntityManager& em, float a, float b, float c)
{
    ::selvaRenderWorld(engine, em, a, b, c);
}
} // namespace selva::gameplay
