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
#include "audio/Audio.h"
#include "combat/ActorVolumes.h"
#include "combat/AttackChain.h"
#include "combat/AttackResolution.h"
#include "combat/ChainObserver.h"
#include "combat/CombatData.h"
#include "combat/CombatLog.h"
#include "combat/HitDetection.h"
#include "combat/HitFeedback.h"
#include "combat/HitVolumes.h"
#include "combat/PlayerEquipment.h"
#include "combat/PressMapping.h"
#include "combat/SpliceDiag.h"
#include "combat/TransitionProfile.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"
#include "gameplay/Enemies.h"
#include "gameplay/LocomotionStateMachine.h"
#include "gameplay/PlayerState.h"
#include "gameplay/TickState.h"
#include "render/Camera.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"
#include "render/SkyPass.h"
#include "render/TerrainShader.h"
#include "render/TreeShader.h"
#include "render/WorldRenderer.h"
#include "ui/ComboHud.h"
#include "world/Collision.h"
#include "world/Terrain.h"

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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <glad/glad.h>

// File-scope aliases mirroring main.cpp conventions so transplanted
// gameplay code compiles unchanged.
//
// `sPlayer` / `sSampler` route into the unified actor pool: the
// player is actors()[0]. Defined as macros so they resolve fresh
// each call (after initPlayer() the pool is non-empty; before, any
// access crashes — same precondition as the old globals had).
static selva::anim::Skeleton& sSkeleton = selva::anim::skeleton();
static selva::anim::SkeletalMesh& sPlayerMesh = selva::anim::playerMesh();
static selva::anim::ClipRegistry& sClips = selva::anim::clips();
static selva::anim::LocomotionConfig& sLocomotionConfig = selva::anim::locomotionConfig();
using selva::gameplay::Actor;
using selva::gameplay::PlayerState;
#define sPlayer (selva::gameplay::player())
#define sSampler (selva::gameplay::player().sampler)
using selva::gameplay::CombatStance;
using selva::gameplay::CombatStanceFoot;
using selva::gameplay::LocomotionStateMachine;
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
static bool sPrevMMB = false;
static bool sPrevR = false;
static bool sPrevGripToggle = false;
static bool sPrevJump = false;

// Scales the clip-authored hip delta applied to sPlayer.pos. 1.0 =
// authored distance (default for rolls, sprint-jumps, all one-shots).
// <1.0 = same animation playback speed but shorter ground travel.
// Used by the walking jump so a walk-speed jump uses the running_jump
// clip but covers less distance (no clip retiming). Set on one-shot
// fire; auto-resets to 1.0 when no one-shot is active.
static float sOneShotHipDeltaScale = 1.0f;

// Active attack hitbox state. The hitbox capsule is parented to a
// joint declared in the firing attack's JSON (WeaponAttack::
// hitbox_joint) — e.g. mixamorig:LeftHand for a jab,
// mixamorig:RightHand for a hook, mixamorig:RightFoot for a kick,
// the weapon hand for a sword (with hitbox_tip_offset_z extending
// the capsule along the blade). The C++ side never picks a joint
// itself; the attack data is the single source of truth.
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
// Clip names for the active block lifecycle (raise → idle loop →
// lower). Set when a block fires; consumed by the loco-override (idle)
// and the release path (lower). nullptr lower = no release one-shot
// (buckler path doesn't have one yet).
static const char* sActiveBlockIdleClip = nullptr;
static const char* sActiveBlockLowerClip = nullptr;

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
// Asymmetric commit time for loco-clip swaps. A cross-family swap
// (forward/back ↔ strafe) takes kLocoCrossFamilyCommitSeconds of
// sustained intent before firing — keyboard digital input naturally
// produces 100-300ms "I touched A while holding W" gestures that
// the player doesn't intend as strafe commits. Same-family swaps
// (walking → running, strafe_left → strafe_right) and idle
// transitions fire immediately because they're either deliberate
// (sprint key pressed) or responsive (stop moving, start moving).
//
// sPendingLocoClip records the picker's choice when it differs
// from the active clip AND requires commit. sPendingLocoStart is
// the wallclock when that pending choice was first seen.
static std::string sPendingLocoClip;
static float sPendingLocoStart = 0.0f;
constexpr float kLocoCrossFamilyCommitSeconds = 0.25f;
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
                                   float playback_rate, const char* clip_key = "",
                                   float freeze_at_seconds = 0.0f)
{
    selva::combat::fireOneShotWithProfile(clip, profile, start_seconds, playback_rate, sSampler,
                                          clip_key, freeze_at_seconds);
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
// on sliders don't aim the camera. While lock-on is engaged: mouse
// input is ignored entirely; cameraYaw is driven from the player↔
// target axis (handled separately in tickLockOnCamera), pitch holds
// at its pre-lock value.
static void tickMouseLook(const selva::tuning::Tunables& tun)
{
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    if (sShowTuningPanel)
        return;
    if (sPlayer.lock_target_idx >= 0)
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
// outgoing one-shot's current frame. Matches BOTH hands AND feet so
// the splice picks a frame where ALL contact-relevant joints line
// up with the live pose — feet-only-hands match was producing
// visible foot snaps when chaining jab -> hook -> combo because
// hands lined up but feet didn't. Returns -1 when source clip /
// joints unresolvable (caller falls back to loco-derived start).
static float chainLinkPoseMatchStart(const selva::anim::AnimationClip& clip,
                                     const char* prev_clip_name, float prev_clip_time)
{
    const auto* prev_clip = sClips.get(prev_clip_name);
    if (prev_clip == nullptr || !prev_clip->isLoaded())
        return -1.0f;
    std::vector<int> joints;
    for (const char* name :
         {"mixamorig:RightHand", "mixamorig:LeftHand", "mixamorig:RightFoot", "mixamorig:LeftFoot"})
    {
        const int idx = sSampler.findJoint(name);
        if (idx >= 0)
            joints.push_back(idx);
    }
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
// Per-attack poise damage from JSON; fall back to the attacker's
// unarmed baseline if the JSON didn't set it. Heavy committed attacks
// declare large values; jabs declare small. See WeaponAttack::poise_damage.
static int resolveAttackPoiseDamage(const selva::combat::WeaponAttack* atk)
{
    if (atk != nullptr && atk->poise_damage > 0.0f)
        return static_cast<int>(std::floor(atk->poise_damage));
    return static_cast<int>(std::floor(sPlayer.body.unarmed_poise_damage));
}

// No chain bookkeeping. Returns true on successful fire.
// `one_shot_active` selects the splice strategy: chainLink (pose-match
// against the live one-shot) vs firstStrike (start clean from t=0).
// The default — "use chainLink when any one-shot is live" — is right
// for the common case (attack -> next attack in a chain). The
// post-dodge handoff overrides via `force_first_strike` because a
// dodge's mid-roll pose has no useful pose-match in an attack clip;
// pose-matching would land on a late frame and visibly truncate the
// punch. The caller knows the splice is cross-family; this function
// doesn't and shouldn't.
static bool fireClipForHand(selva::combat::HandSide hand, const char* clip_name,
                            float combo_input_buffer_seconds, bool force_first_strike = false)
{
    const auto* clip = sClips.get(clip_name);
    if (clip == nullptr || !clip->isLoaded())
        return false;
    const bool one_shot_active = sSampler.isOneShotActive() && !force_first_strike;
    const auto fd_pre = sSampler.frameDiagnostics();

    float pose_matched_start = -1.0f;
    if (one_shot_active && fd_pre.one_shot_name != nullptr)
    {
        pose_matched_start =
            chainLinkPoseMatchStart(*clip, fd_pre.one_shot_name, fd_pre.one_shot_time);
    }
    else
    {
        // First-strike: per-attack first_strike_start_seconds takes
        // priority over the auto pose-match. Used by clips whose t=0
        // pose is far from any plausible live gait pose (e.g. the
        // running flying-knee windup) so the JSON can pin the splice
        // past the windup directly. Negative = fall through to
        // pose-match.
        const auto* atk = findAttackForClip(clip_name);
        if (atk != nullptr && atk->first_strike_start_seconds >= 0.0f)
            pose_matched_start = atk->first_strike_start_seconds;
        else
            pose_matched_start = poseMatchStartFromLoco(*clip, 0.30f);
    }
    const float start_seconds = (pose_matched_start >= 0.0f) ? pose_matched_start : 0.0f;

    TransitionProfile profile = one_shot_active ? profiles::chainLink() : profiles::firstStrike();
    profile.lockout = TransitionProfile::Lockout::None;
    const float blend_override = perAttackBlendOutOverride(clip_name);
    if (blend_override >= 0.0f)
        profile.blend_out_seconds = blend_override;

    const float rate = effectiveAttackPlaybackRate(hand);
    fireOneShotWithProfile(*clip, profile, start_seconds, rate, clip_name);
    setHandCancelWindow(hand, clip_name, *clip, rate, combo_input_buffer_seconds);
    combatLog("[combat:fire] hand=%s clip=%s dur=%.3fs start=%.3fs rate=%.2f one_shot_active=%d "
              "player_pos=(%.3f, %.3f)\n",
              (hand == selva::combat::HandSide::Right) ? "R" : "L", clip_name, clip->duration(),
              start_seconds, rate, one_shot_active ? 1 : 0, sPlayer.pos.x, sPlayer.pos.z);

    // Spawn the attack hitbox parented to the swinging hand. The
    // hitbox tracks the joint each frame (see updateActiveAttackHitbox)
    // until its lifetime expires. Once-per-swing memo prevents
    // double-hits across frames. Lifetime ~= active swing phase
    // (roughly half the clip duration after the start offset);
    // per-attack JSON authoring will replace this default later.
    {
        // Resolve the hitbox joint + shape from the attack's JSON
        // data. Fall back to the weapon's grip bone_right (the
        // standard weapon hand) if hitbox_joint is unset — most
        // weapons swing from the same right hand the bone_right
        // attaches to, so the override is only needed for off-hand
        // animations (jab) or non-hand strikes (kicks).
        const auto* atk = findAttackForClip(clip_name);
        const char* joint_name = nullptr;
        float hitbox_radius = 0.18f;
        float hitbox_tip_offset_z = 0.0f;
        if (atk != nullptr && !atk->hitbox_joint.empty())
            joint_name = atk->hitbox_joint.c_str();
        if (atk != nullptr && atk->hitbox_radius > 0.0f)
            hitbox_radius = atk->hitbox_radius;
        if (atk != nullptr)
            hitbox_tip_offset_z = atk->hitbox_tip_offset_z;
        if (joint_name == nullptr)
        {
            const auto* w = sEquipment.right;
            if (w != nullptr && w->cls != nullptr && !w->cls->attach.bone_right.empty())
                joint_name = w->cls->attach.bone_right.c_str();
        }
        const int joint_idx = (joint_name != nullptr) ? sSampler.findJoint(joint_name) : -1;
        if (joint_idx >= 0)
        {
            selva::combat::AttackHitboxSpawnParams sp;
            sp.actor = &sPlayer;
            sp.attacker = selva::combat::OwnerRef{selva::combat::OwnerKind::Player, 0};
            sp.attacker_faction = sPlayer.faction;
            sp.raw_damage = selva::gameplay::computeAttackDamage(
                sPlayer.stats, sPlayer.body.unarmed_damage, 0.5f, 0.5f);
            sp.poise_damage = resolveAttackPoiseDamage(atk);
            sp.joint_name = joint_name;
            sp.hitbox_radius = hitbox_radius;
            sp.hitbox_tip_offset_z = hitbox_tip_offset_z;
            sp.clip_duration_seconds = clip->duration();
            sp.clip_start_seconds = start_seconds;
            sp.playback_rate = rate;
            sp.mesh_foot_offset_y = sPlayerMesh.foot_offset_y;
            selva::combat::resetHitMemo();
            selva::combat::spawnAttackHitbox(sp);
            // active_attack_* fields on sPlayer are written by
            // spawnAttackHitbox so updateActiveAttackHitbox can
            // re-anchor the volume each frame as the hand swings.
        }
    }
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

// Log one-shot active state edges (active=1 on fire, active=0 on
// BlendOut complete). Used by combat-debug.log to mark when a
// one-shot's pose-dominance starts/ends.
static void logOneShotStateTransitions()
{
    static bool sPrevOneShotActive = false;
    const bool now = sSampler.isOneShotActive();
    if (sPrevOneShotActive != now && selva::combat::isCombatDebugEnabled())
    {
        const auto fd = sSampler.frameDiagnostics();
        combatLog("[combat:one-shot %.4fs] active=%d weight=%.3f phase=%d clip=%s\n",
                  selva::wallClock(), now ? 1 : 0, fd.one_shot_weight, fd.one_shot_phase,
                  fd.one_shot_name ? fd.one_shot_name : "(none)");
    }
    sPrevOneShotActive = now;
}

// Post-dodge attack handoff. Fires the buffered press as soon as the
// dodge one-shot is past its cancel_fraction (the same gate that
// allows roll → roll chaining) — not after the full blend-out. Waiting
// for blend-out let the locomotion track resume (running) before the
// punch fired, which read as "running, then punch" instead of
// "rolling, then punch." Returns true if a clip fired.
static bool tryFirePostDodgeAttack(bool shift_held, float combo_input_buffer_seconds)
{
    if (!sPostDodgeAttack.pending)
        return false;
    // Allow the splice once the dodge has committed past its cancel
    // fraction. If no one-shot is active at all (dodge already fully
    // ended), the original gate is also satisfied.
    if (sSampler.isOneShotActive() && !sSampler.isOneShotPastCancelFraction())
        return false;
    // The dodge timer can still be ticking down a hair past the
    // cancel point — don't gate on it.
    (void)sDodgeActive;
    const float wait_seconds = selva::wallClock() - sPostDodgeAttack.buffered_at;
    const selva::combat::PressModifiers mods{shift_held, sPlayer.sprinting};
    const auto& dw = selva::combat::cancelWindow(sPostDodgeAttack.hand);
    const char* clip_name =
        selva::combat::clipForButton(sEquipment, sPostDodgeAttack.hand, sPostDodgeAttack.button,
                                     mods, selva::wallClock(), dw.open_at, dw.close_at);
    bool fired = false;
    if (clip_name != nullptr &&
        fireClipForHand(sPostDodgeAttack.hand, clip_name, combo_input_buffer_seconds,
                        /*force_first_strike=*/true))
    {
        fired = true;
        combatLog("[combat:rhythm %.4fs] post-dodge attack FIRED (waited %.3fs since press)\n",
                  selva::wallClock(), wait_seconds);
    }
    sPostDodgeAttack.pending = false;
    return fired;
}

// Velocity-driven locomotion physics. Accelerates the player's
// ground-plane velocity toward a target speed (idle when no input,
// walk_speed when WASD held, run_speed when WASD+sprinting). On
// release, decelerates toward zero. Acceleration vs deceleration are
// separately tunable.
//
// Why velocity: the previous discrete (Idle/Walk/Run) state machine
// produced visible artifacts on simultaneous WASD+Sprint edges
// because the two inputs had different debounce latencies — the SM
// would briefly see a mismatched (is_moving, is_sprinting) edge and
// pick Walk between Run and Idle. Velocity has no discrete states;
// the animation system reads the magnitude continuously and weights
// idle/walk/run clips smoothly. No transition clips, no debounce,
// no mismatched edges.
//
// `movement_locked` (dodge active OR full-mask one-shot) freezes
// velocity in place — gameplay code (dodge clip's hip motion) drives
// world translation directly during those windows.
// Most-recent target speed (m/s) computed by tickPlayerVelocity. Read
// by the [sm loco-pick] logger so the trace shows intent (where
// velocity is heading) alongside current speed (where it is now). Lets
// us see whether the visible idle->walking->running cascade comes
// from the velocity ramp or from an actual intent change.
static float sLastTargetSpeed = 0.0f;

// The locomotion decision for this frame, made ONCE at the top of
// selvaPerFrame before tickPlayerVelocity / applyPerFrameTranslation.
// Both consult it so the picker isn't called multiple times per
// frame (which would be safe today since it's pure, but invites
// drift if state ever creeps into the picker). The bilateral
// translation-source contract (extract-side in sampler, apply-side
// in gameplay) reads `source` from here so both halves agree on
// the SAME picked clip's declaration. See
// feedback_data_driven_over_convention.md.
struct LocomotionFrameDecision
{
    std::string clip_name; // empty = no override; speed-based free-mode pick will fill this in
    selva::anim::TranslationSource source = selva::anim::TranslationSource::Velocity;
};
static LocomotionFrameDecision sLocoDecision;

// Forward decls: these live near the picker but are called from
// tickPlayerVelocity / applyPerFrameTranslation via sLocoDecision.
static const char* selectLockedLocomotionClip(const glm::vec3& moveIntent);
static const char* selectLocomotionClipFromSpeed(float target_speed, bool is_armed,
                                                 const selva::tuning::Tunables& tun);

// Run the top-of-frame locomotion decision: layers 1-4 from the
// picker chain (debug override, locked combat directional, speed-
// based free-mode, held-block). Layer 5 (loco-freeze) is applied
// later inside selectLocomotionClip because it depends on sampler
// state that can change later in the frame. Translation source
// comes from the active clip's locomotion.json declaration.
//
// Also pins combat_stance = CombatReady while locked so the
// speed-based idle branch returns unarmed_combat_idle (not
// standard_idle) when the player is locked-but-standing-still.
// The pin is re-asserted every frame; tickCombatStance later
// honors it via its lock-aware moving-clear gate.
static void runLocomotionDecision(const glm::vec3& moveIntent, const selva::tuning::Tunables& tun)
{
    sLocoDecision = LocomotionFrameDecision{};

    if (sPlayer.lock_target_idx >= 0)
    {
        sLocomotionSM.combat_stance = selva::gameplay::CombatStance::CombatReady;
        sLocomotionSM.stance_active_until = selva::wallClock() + tun.combat_idle_grace_seconds;
    }

    if (!sDebugClipName.empty())
    {
        sLocoDecision.clip_name = sDebugClipName;
    }
    else if (const char* locked = selectLockedLocomotionClip(moveIntent); locked != nullptr)
    {
        sLocoDecision.clip_name = locked;
    }
    else
    {
        // Free-mode speed-based pick. Substitute intent-magnitude →
        // intended target speed (walk or run) so the decision is
        // pre-velocity (tickPlayerVelocity hasn't run yet this
        // frame). sLastTargetSpeed from the previous frame is too
        // stale for a fresh-press edge.
        const float intent_mag = glm::length(moveIntent);
        const float would_be_target = (intent_mag <= 0.0001f) ? 0.0f
                                      : sPlayer.sprinting     ? tun.run_speed
                                                              : tun.walk_speed;
        const bool is_armed = !isUnarmed(sEquipment);
        sLocoDecision.clip_name = selectLocomotionClipFromSpeed(would_be_target, is_armed, tun);
    }

    if (sBlockingActive && sActiveBlockIdleClip != nullptr)
        sLocoDecision.clip_name = sActiveBlockIdleClip;

    // Cross-family commit: a swap from forward/back ↔ strafe requires
    // sustained intent (the player has to keep that input held for
    // kLocoCrossFamilyCommitSeconds) before the clip swaps. Same-
    // family changes (walking↔running, strafe-left↔strafe-right,
    // walking↔walking_backward) and any transition involving an
    // idle clip fire immediately.
    auto family = [](const std::string& name) -> int
    {
        if (name == "walking" || name == "running" || name == "walking_backward" ||
            name == "running_backward")
            return 1; // fwd/back
        if (name == "strafe_walking_left" || name == "strafe_walking_right" ||
            name == "strafe_running_left" || name == "strafe_running_right")
            return 2; // strafe
        return 0;     // idle / other
    };
    const int desired_fam = family(sLocoDecision.clip_name);
    const int current_fam = family(sLastLocoClipName);
    const bool cross_family =
        desired_fam == 1 && current_fam == 2 || desired_fam == 2 && current_fam == 1;
    if (cross_family && sLocoDecision.clip_name != sLastLocoClipName)
    {
        if (sPendingLocoClip != sLocoDecision.clip_name)
        {
            sPendingLocoClip = sLocoDecision.clip_name;
            sPendingLocoStart = selva::wallClock();
        }
        if ((selva::wallClock() - sPendingLocoStart) < kLocoCrossFamilyCommitSeconds)
            sLocoDecision.clip_name = sLastLocoClipName;
        else
            sPendingLocoClip.clear();
    }
    else
    {
        // Either same-family swap, idle transition, or no swap. Clear
        // any pending cross-family intent — the player either
        // committed to it (we landed on it) or moved on.
        sPendingLocoClip.clear();
    }

    sLocoDecision.source = sLocomotionConfig.translationSource(sLocoDecision.clip_name);
}

static void tickPlayerVelocity(const glm::vec3& moveIntent, bool movement_locked,
                               const selva::tuning::Tunables& tun, float dt)
{
    // Intent speed is computed unconditionally — even when the
    // velocity gate is locked (dodge / full-mask one-shot), the SM
    // needs to know what the player WANTS so the loco track stays
    // on walking/running and doesn't drift to combat_idle during an
    // attack-while-running. The lock only freezes actual velocity,
    // not intent.
    const float intent_mag = glm::length(moveIntent);
    glm::vec2 target_velocity(0.0f);
    // RootMotion clips drive world translation themselves via
    // consumedHipDelta(); velocity must stay zero or it stacks with
    // the clip's hip motion (the treadmill bug from earlier in the
    // session). Only Velocity-source clips get a velocity ramp.
    const bool use_velocity = (sLocoDecision.source == selva::anim::TranslationSource::Velocity);
    if (intent_mag > 0.0001f && use_velocity)
    {
        const glm::vec3 dir = moveIntent / intent_mag;
        const float target_speed = sPlayer.sprinting ? tun.run_speed : tun.walk_speed;
        target_velocity = glm::vec2(dir.x, dir.z) * target_speed;
    }
    // sLastTargetSpeed feeds the idle-vs-walk gate in the picker.
    // For RootMotion clips, velocity is zero so substitute intent
    // magnitude — same purpose, just sourced from input.
    sLastTargetSpeed = use_velocity ? glm::length(target_velocity) : intent_mag;
    if (movement_locked)
    {
        sPlayer.velocity_xz = glm::vec2(0.0f);
        return;
    }
    const glm::vec2 delta = target_velocity - sPlayer.velocity_xz;
    const float delta_mag = glm::length(delta);
    if (delta_mag <= 0.0001f)
    {
        sPlayer.velocity_xz = target_velocity;
        return;
    }
    // Choose accel vs decel based on direction of velocity change.
    // Speeding up = accel (any case where the new speed is higher OR
    // direction-changing toward a new target). Slowing down = decel
    // (target speed is lower OR target is zero). Magnitude check is
    // adequate for our needs; future polish could split tangential
    // vs radial components.
    const float current_speed = glm::length(sPlayer.velocity_xz);
    const float target_speed = glm::length(target_velocity);
    const float rate =
        (target_speed >= current_speed) ? tun.locomotion_accel : tun.locomotion_decel;
    const float max_step = rate * dt;
    if (delta_mag <= max_step)
        sPlayer.velocity_xz = target_velocity;
    else
        sPlayer.velocity_xz += (delta / delta_mag) * max_step;
}

// ---------------------------------------------------------------------------
// Lock-on: combat-mode toggle that drives yaw, camera, movement basis,
// and the directional locomotion picker. Symbol/reticle is a
// placeholder white square; refined in Phase B.
// ---------------------------------------------------------------------------

// Tunables for Phase A. File-locals while values are dialed in code;
// promote to Tunables.json once they feel right. Reticle chest offset
// lives next to its draw call in ui/ActorHud.cpp.
constexpr float kLockOnConeHalfAngleRadians = 0.5236f; // 30°
constexpr float kLockOnAcquireRangeMeters = 15.0f;

// Camera-forward direction on XZ from cameraYaw(). Mirrors the camFwd
// construction in computeMoveIntent so the cone math reads identically.
static glm::vec3 cameraForwardXZ()
{
    const float yaw = selva::render::cameraYaw();
    return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
}

// Acquire the nearest hostile, live enemy within the camera-forward
// cone. Returns its pool index or -1 if none. Faction filter:
// Faction::Hostile only; companions and neutrals are excluded from
// targeting in v1.
static int acquireLockOnTarget()
{
    const glm::vec3 cam_fwd = cameraForwardXZ();
    auto& pool = selva::gameplay::actors();
    const glm::vec3 player_pos = pool[0].pos;
    int best_idx = -1;
    float best_dist_sq = kLockOnAcquireRangeMeters * kLockOnAcquireRangeMeters;
    for (std::size_t i = 1; i < pool.size(); ++i)
    {
        const Actor& a = pool[i];
        if (a.faction != selva::gameplay::Faction::Hostile)
            continue;
        if (a.is_dead)
            continue;
        const glm::vec3 to = a.pos - player_pos;
        const glm::vec3 to_xz(to.x, 0.0f, to.z);
        const float dist_sq = glm::dot(to_xz, to_xz);
        if (dist_sq < 1e-6f || dist_sq > best_dist_sq)
            continue;
        const float dist = std::sqrt(dist_sq);
        const glm::vec3 to_dir = to_xz / dist;
        const float dot_fwd = glm::dot(to_dir, cam_fwd);
        if (dot_fwd < std::cos(kLockOnConeHalfAngleRadians))
            continue;
        best_dist_sq = dist_sq;
        best_idx = static_cast<int>(i);
    }
    return best_idx;
}

// Player's lock target (thin wrapper over the shared
// selva::gameplay::resolveLockTarget for call-site readability).
static Actor* resolveLockTarget()
{
    return selva::gameplay::resolveLockTarget(sPlayer);
}

// Per-frame lock-on input + auto-release pass. Middle-mouse rising
// edge toggles the lock; sprint engaging and target death auto-
// release. Must run BEFORE consumers of the lock state (camera, yaw,
// movement intent, locomotion picker).
static void tickLockOnInput()
{
    const Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    const bool mmb_now = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    const bool press_mmb = mmb_now && !sPrevMMB;
    sPrevMMB = mmb_now;

    // Auto-release on target death (or stale pool index). Sprint
    // does NOT release lock — sprint is part of combat locomotion
    // (running directional clips), not an exit gesture.
    if (sPlayer.lock_target_idx >= 0)
    {
        const Actor* t = resolveLockTarget();
        if (t == nullptr || t->is_dead)
            sPlayer.lock_target_idx = -1;
    }

    if (!press_mmb || sShowTuningPanel)
        return;

    if (sPlayer.lock_target_idx >= 0)
    {
        sPlayer.lock_target_idx = -1;
        return;
    }

    sPlayer.lock_target_idx = acquireLockOnTarget();
}

// While locked, drive cameraYaw from the player→target XZ axis so
// the third-person camera lines up looking at the target over the
// player's shoulder. Pitch is left wherever tickMouseLook last left
// it before lock engaged (mouse-Y ignored during lock).
static void tickLockOnCamera()
{
    const Actor* target = resolveLockTarget();
    if (target == nullptr)
        return;
    const glm::vec3 to(target->pos.x - sPlayer.pos.x, 0.0f, target->pos.z - sPlayer.pos.z);
    if (glm::dot(to, to) <= 1e-6f)
        return;
    selva::render::setCameraYaw(yawFromGroundDir(glm::normalize(to)));
}

// Smooth-turn the player's yaw toward the move-intent direction.
// The rate is a piecewise lerp between `turn_rate_min` (small
// deltas) and `turn_rate_max` (180° flips) based on |delta|/π.
// A 30° course correction stays smooth; a 180° turn snaps fast
// enough that "press W then S" feels responsive in combat.
// No-op when movement is locked or intent is zero. Yaw stays
// gameplay-driven; translation reads the sampler's hip delta after.
static void tickPlayerYaw(const glm::vec3& moveIntent, bool movement_locked, float turn_rate_min,
                          float turn_rate_max, float dt)
{
    if (movement_locked)
        return;
    // Lock-on overrides input-driven yaw: snap to the target each frame
    // so the player always faces it. Translation reads sPlayer.yaw too,
    // so the 4 directional combat clips will play with respect to a
    // facing that's correct for the locked-mode WASD basis.
    if (const Actor* target = resolveLockTarget(); target != nullptr)
    {
        const glm::vec3 to(target->pos.x - sPlayer.pos.x, 0.0f, target->pos.z - sPlayer.pos.z);
        if (glm::dot(to, to) > 1e-6f)
            sPlayer.yaw = yawFromGroundDir(glm::normalize(to));
        return;
    }
    if (glm::length(moveIntent) <= 0.0001f)
        return;
    const glm::vec3 dir = glm::normalize(moveIntent);
    const float targetYaw = yawFromGroundDir(dir);
    float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
    constexpr float kPi = 3.1415927f;
    // |delta|/π is 0 for a tiny correction and 1 for a 180° flip.
    // Lerp the rate so reversals snap and small turns stay smooth.
    const float t = std::min(1.0f, std::abs(delta) / kPi);
    const float rate = turn_rate_min + (turn_rate_max - turn_rate_min) * t;
    const float maxStep = rate * dt;
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

// R press handler: toggle combat stance, or flip stance foot when
// shift-held and already in CombatReady. Returns true if the press
// should mark combat_input_this_frame (entry into CombatReady).
static bool tryHandleStanceTogglePress(bool press_r, bool shift_held)
{
    if (!press_r)
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

// The block-lifecycle clip set: raise (one-shot), idle (looping
// loco-track override), lower (release one-shot, nullable).
struct BlockClipSet
{
    const char* raise;
    const char* idle;
    const char* lower;
};

// Fire a block raise one-shot and stash the idle/lower clip names
// for the lifecycle's later phases. `loco_settled` selects the
// blockFromLatch vs blockLive profile.
static void fireBlockOneShot(bool loco_settled, const BlockClipSet& set)
{
    const auto* raise_clip = sClips.get(set.raise);
    if (raise_clip == nullptr || !raise_clip->isLoaded())
        return;
    const auto fd_pre = sSampler.frameDiagnostics();
    // Optional JSON-override for raise start; default 0 (play from frame 0).
    float block_start = 0.0f;
    if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr &&
        sEquipment.right->cls->block_clip_start_seconds >= 0.0f)
        block_start = sEquipment.right->cls->block_clip_start_seconds;
    const TransitionProfile profile =
        loco_settled ? profiles::blockFromLatch() : profiles::blockLive();
    fireOneShotWithProfile(*raise_clip, profile, block_start, /*playback_rate=*/1.0f, set.raise);
    sBlockingActive = true;
    sActiveBlockIdleClip = set.idle;
    sActiveBlockLowerClip = set.lower;
    combatLog("[combat:block-fire] mode=%s raise=%s idle=%s lower=%s loco=%s@%.3fs "
              "prev_1shot=%s(phase=%d,w=%.2f)\n",
              loco_settled ? "SETTLED" : "SNAP", set.raise, set.idle ? set.idle : "(none)",
              set.lower ? set.lower : "(none)",
              fd_pre.loco_current_name ? fd_pre.loco_current_name : "(none)",
              fd_pre.loco_current_time, fd_pre.one_shot_name ? fd_pre.one_shot_name : "(none)",
              fd_pre.one_shot_phase, fd_pre.one_shot_weight);
}

// Resolve the block-lifecycle clip set for the current equipment +
// modifiers. Buckler uses sword_and_shield_block + _idle (no lower
// yet). Unarmed+shift uses the 3-clip set baked from Mixamo's
// Center Block. Returns nullptr raise = caller treats RMB as a
// normal attack input.
static BlockClipSet resolveBlockClip(bool shift_held)
{
    if (offHandCanBlock(sEquipment))
        return {"sword_and_shield_block", "sword_and_shield_block_idle", nullptr};
    if (isUnarmed(sEquipment) && shift_held)
        return {"unarmed_block_raise", "unarmed_block_idle", "unarmed_block_lower"};
    return {nullptr, nullptr, nullptr};
}

// RMB → block routing. Press from Peaceful arms a delayed first-
// action (so loco settles into combat-idle); subsequent press fires
// directly. Release ends a held block — fires the lower one-shot
// if the equipment has one. Returns true if any combat input fired.
static bool tickBlockingFromRMB(const BlockClipSet& set, bool press_rmb, bool release_rmb,
                                float combat_entry_delay_seconds)
{
    bool fired = false;
    if (press_rmb)
    {
        const bool from_peaceful = (sLocomotionSM.combat_stance == CombatStance::Peaceful);
        if (from_peaceful && !sPendingFirstAction.active)
        {
            sPendingFirstAction.active = true;
            sPendingFirstAction.block_clip = set.raise;
            sPendingFirstAction.block_idle_clip = set.idle;
            sPendingFirstAction.block_lower_clip = set.lower;
            sPendingFirstAction.fire_at = selva::wallClock() + combat_entry_delay_seconds;
            fired = true;
        }
        else if (!sPendingFirstAction.active)
        {
            fireBlockOneShot(false, set);
            fired = true;
        }
    }
    else if (release_rmb && sBlockingActive)
    {
        if (sActiveBlockLowerClip != nullptr)
        {
            // Fire the lower one-shot. The current loco-track idle
            // override clears as sBlockingActive flips false; the
            // one-shot plays over the resuming combat-idle.
            const auto* lower = sClips.get(sActiveBlockLowerClip);
            if (lower != nullptr && lower->isLoaded())
            {
                const TransitionProfile profile = profiles::blockLive();
                fireOneShotWithProfile(*lower, profile, 0.0f, /*playback_rate=*/1.0f,
                                       sActiveBlockLowerClip);
            }
        }
        else
        {
            sSampler.releaseOneShot();
        }
        sBlockingActive = false;
        sActiveBlockIdleClip = nullptr;
        sActiveBlockLowerClip = nullptr;
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
    const BlockClipSet set{sPendingFirstAction.block_clip, sPendingFirstAction.block_idle_clip,
                           sPendingFirstAction.block_lower_clip};
    fireBlockOneShot(true, set);
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
//   * Active one-shot is past its cancel_fraction (chain-roll or
//     chain-jump: roll/jump-into-dodge near the recovery tail).
// Buffer drops on fire OR on combo_input_buffer_seconds expiry.
static bool tryFireBufferedDodge(float backstep_playback_rate, float roll_playback_rate,
                                 float combo_input_buffer_seconds)
{
    if (!sBufferedDodge.pending)
        return false;
    const float age = selva::wallClock() - sBufferedDodge.buffered_at;
    if (age > combo_input_buffer_seconds)
    {
        sBufferedDodge.pending = false;
        return false;
    }
    const bool one_shot_cancel_open = sSampler.isOneShotPastCancelFraction();
    const bool gate_open =
        !sSampler.isOneShotActive() || eitherHandCancelWindowOpen() || one_shot_cancel_open;
    if (!gate_open)
        return false;
    const bool fired = fireDodgeFromTap(sBufferedDodge.move_intent_at_press, backstep_playback_rate,
                                        roll_playback_rate);
    sBufferedDodge.pending = false;
    if (fired)
        combatLog("[combat:rhythm %.4fs] buffered dodge FIRED (waited %.3fs since press, "
                  "via=%s)\n",
                  selva::wallClock(), age,
                  one_shot_cancel_open ? "one-shot-cancel"
                                       : (sSampler.isOneShotActive() ? "attack-cancel" : "clean"));
    return fired;
}

// Rising-edge F fires a jump one-shot. Variant picked by WASD intent
// (not current velocity, which is zeroed during movement_locked).
//   * No WASD → `jumping` (vertical, in place, 1.10x rate)
//   * WASD → `running_jump` (forward leap; sprinting=full distance,
//     walking=0.55x hip-delta scale for shorter travel)
// Uses profiles::jump() so chain-jumps work via the unified
// cancel_fraction model.
static void tickJumpInput(const Uint8* keys)
{
    const bool jumpNow = (keys[SDL_SCANCODE_F] != 0);
    if (!(jumpNow && !sPrevJump))
    {
        sPrevJump = jumpNow;
        return;
    }
    const bool wasd_held = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_S] ||
                           keys[SDL_SCANCODE_D];
    const char* clip_name = wasd_held ? "running_jump" : "jumping";
    const auto* clip = sClips.get(clip_name);
    const bool gate_open = !sSampler.isOneShotActive() || sSampler.isOneShotPastCancelFraction();
    if (clip != nullptr && clip->isLoaded() && gate_open)
    {
        TransitionProfile profile = profiles::jump();
        float rate = 1.10f;
        sOneShotHipDeltaScale = 1.0f;
        if (wasd_held)
        {
            profile.blend_out_seconds = 0.30f;
            rate = 1.0f;
            if (!sPlayer.sprinting)
                sOneShotHipDeltaScale = 0.55f;
        }
        fireOneShotWithProfile(*clip, profile, 0.0f, rate, clip_name);
    }
    sPrevJump = jumpNow;
}

// Try to fire a dodge from a Space release-edge tap. If a one-shot or
// dodge is already in flight, buffer the press so the per-frame fire
// path can fire it when its cancel gate opens. Returns true on
// immediate fire.
static bool fireOrBufferDodgeOnSpaceRelease(const glm::vec3& moveIntent,
                                            float backstep_playback_rate, float roll_playback_rate)
{
    const bool blocked = sSampler.isOneShotActive() || sDodgeActive;
    if (!blocked)
        return fireDodgeFromTap(moveIntent, backstep_playback_rate, roll_playback_rate);
    sBufferedDodge.pending = true;
    sBufferedDodge.move_intent_at_press = moveIntent;
    sBufferedDodge.buffered_at = selva::wallClock();
    combatLog("[combat:rhythm %.4fs] dodge BUFFERED (%s in flight)\n", selva::wallClock(),
              sDodgeActive ? "dodge" : "one-shot");
    return false;
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
        // Commit to sprint when EITHER Space is held past the
        // tap-window OR WASD is held (movement intent is
        // unambiguous, no reason to wait).
        const bool wasd_held = keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_A] ||
                               keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_D];
        if (sSpaceHeldSeconds >= dodge_tap_window || wasd_held)
            sPlayer.sprinting = true;
    }
    if (release_edge)
    {
        const bool was_tap = sSpaceHeldSeconds < dodge_tap_window;
        if (was_tap && !sDodgeFiredThisPress)
            fired = fireOrBufferDodgeOnSpaceRelease(moveIntent, backstep_playback_rate,
                                                    roll_playback_rate);
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
    const float speed_now = glm::length(sPlayer.velocity_xz);
    // Also log the locked-mode picker inputs (lock state, target-
    // axis fwd/right dots, sprint) so transitions are diagnosable
    // without re-running. Player-side only.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    const Actor* lt = resolveLockTarget();
    const bool locked = (lt != nullptr);
    combatLog("[sm %.4fs] loco-pick %s@%.3fs -> %s (blend=%.3fs speed_now=%.2f target=%.2f "
              "locked=%d sprint=%d WASD=%d%d%d%d)\n",
              selva::wallClock(), sLastLocoClipName.c_str(), fd.loco_current_time,
              clip_name.c_str(), blend_seconds, speed_now, sLastTargetSpeed, locked ? 1 : 0,
              sPlayer.sprinting ? 1 : 0, keys[SDL_SCANCODE_W] ? 1 : 0, keys[SDL_SCANCODE_A] ? 1 : 0,
              keys[SDL_SCANCODE_S] ? 1 : 0, keys[SDL_SCANCODE_D] ? 1 : 0);
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

// Pick the locomotion clip for this frame from the player's
// continuous velocity magnitude. Speed thresholds:
//   * speed <= idle_to_walk_speed  → idle
//   * speed <= walk_to_run_speed   → walking
//   * speed >  walk_to_run_speed   → running
// The idle clip is stance-dependent (combat-ready vs peaceful).
//
// Why velocity instead of (Idle/Walk/Run) state with debounced
// inputs: the SM produced visible artifacts at simultaneous
// WASD+Sprint edges because input signals had different latencies
// (WASD debounced 100ms, sprint instant) and the SM would briefly
// pick a wrong intermediate state. Velocity is a single continuous
// scalar; clip selection reads it without state-machine bookkeeping
// or debouncing. Acceleration / deceleration physics smooth the
// edges naturally — there's no "edge" to mis-time.
//
// The picker reads TARGET speed (where velocity is heading), not
// current speed. Reading current speed produced a visible
// idle->walking->running cascade on a fresh sprint-from-idle: as
// velocity ramped through the 0.30 (idle_to_walk) and 3.0
// (walk_to_run) thresholds, the picker fired three back-to-back
// cross-family transitions in ~150ms, stacking three pose-match
// residuals + inertialization decays on top of each other and
// producing a visible foot/leg spasm. Target speed is the player's
// intent (walk_speed when WASD held; run_speed when WASD+sprint;
// 0 otherwise) and changes only on input edges, so the picker fires
// at most once per intent change. The crossfade hides the velocity
// ramp; intent stays in lockstep with what the player asked for.
static const char* selectLocomotionClipFromSpeed(float target_speed, bool is_armed,
                                                 const selva::tuning::Tunables& tun)
{
    if (target_speed <= tun.idle_to_walk_speed)
    {
        // Combat stance idle when stance is active; peaceful idle
        // otherwise. Stance management still honors stance_active_until.
        const bool combat_ready =
            sLocomotionSM.combat_stance == selva::gameplay::CombatStance::CombatReady;
        if (combat_ready)
            return is_armed ? "sword_and_shield_idle_4" : "unarmed_combat_idle";
        return "standard_idle";
    }
    if (target_speed <= tun.walk_to_run_speed)
        return "walking";
    return "running";
}

// Combat stance lifecycle (preserved from the old SM): CombatReady
// engages on combat_input_this_frame; auto-decays to Peaceful after
// stance_active_until OR sooner if the player commits to sprinting
// (sprinting reads as "not actively threatened" — the body lowers
// its guard to commit to speed, soulslike convention). Without the
// sprint-clear, the grace timer keeps stance latched through a
// run; on stop, loco picks combat-idle, then 2s later the timer
// expires and combat-idle visibly fades to standard-idle. Sprint
// clear collapses that to a single transition.
//
// Velocity model doesn't care about stance directly, but the
// idle clip-pick does (combat-stance idle vs peaceful idle).
static void tickCombatStance(bool combat_input_this_frame, const selva::tuning::Tunables& tun)
{
    if (combat_input_this_frame)
    {
        sLocomotionSM.combat_stance = selva::gameplay::CombatStance::CombatReady;
        const float new_until = selva::wallClock() + tun.combat_idle_grace_seconds;
        if (new_until > sLocomotionSM.stance_active_until)
            sLocomotionSM.stance_active_until = new_until;
    }
    if (sLocomotionSM.combat_stance == selva::gameplay::CombatStance::CombatReady &&
        selva::wallClock() >= sLocomotionSM.stance_active_until)
    {
        sLocomotionSM.combat_stance = selva::gameplay::CombatStance::Peaceful;
    }
    // Any locomotion intent (walk OR sprint) clears stance
    // immediately. Without this, stance stays latched through a
    // walk-cycle: stop → loco picks combat-idle (stance still
    // active), then 2s later grace expires → combat-idle visibly
    // fades to standard-idle, which reads as a delayed cleanup
    // after the player has clearly moved on. Walking past an
    // enemy with weapons down is the natural exit from combat.
    //
    // Suppressed while locked: lock-on IS combat mode, motion
    // doesn't exit it. The pin in selectLocomotionClip re-asserts
    // CombatReady, but doing the clear here would briefly flip the
    // stance Peaceful between the pin and the idle-branch read on
    // any frame where the player was walking and just stopped.
    const bool moving = glm::length(sPlayer.velocity_xz) > 0.05f;
    if (sPlayer.lock_target_idx < 0 && moving &&
        sLocomotionSM.combat_stance == selva::gameplay::CombatStance::CombatReady)
    {
        sLocomotionSM.combat_stance = selva::gameplay::CombatStance::Peaceful;
        sLocomotionSM.stance_active_until = 0.0f;
    }
}

// Pick the directional locomotion clip while locked + moving:
// forward/back/strafe-left/right × walking/running, by dominant-
// axis dispatch of moveIntent against the player→target axis.
// Returns nullptr when not locked or no usable direction; caller
// falls back to the free-mode speed picker (which uses the same
// `walking` / `running` forward clips, producing seamless
// transitions in/out of lock-on).
static const char* selectLockedLocomotionClip(const glm::vec3& moveIntent)
{
    const Actor* target = resolveLockTarget();
    if (target == nullptr)
        return nullptr;
    const glm::vec3 to(target->pos.x - sPlayer.pos.x, 0.0f, target->pos.z - sPlayer.pos.z);
    const float to_len_sq = glm::dot(to, to);
    if (to_len_sq <= 1e-6f)
        return nullptr;
    const glm::vec3 fwd = to / std::sqrt(to_len_sq);
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    return selva::gameplay::directionalLocoClip(fwd, right, moveIntent, sPlayer.sprinting);
}

// Produce a LocomotionPick from the pre-decided clip name in
// sLocoDecision. Layer 5 of the picker chain (loco-freeze) lives
// here because it depends on sampler state (one-shot phase) that
// can change later in the frame than runLocomotionDecision runs.
// Also runs tickCombatStance, which the pin in runLocomotionDecision
// re-asserts each frame.
static LocomotionPick selectLocomotionClip(const glm::vec3& /*moveIntent*/, float /*dt*/,
                                           bool combat_input_this_frame,
                                           const selva::tuning::Tunables& tun)
{
    LocomotionPick pick;
    pick.blend_seconds = tun.anim_blend_seconds;
    pick.loops = true;
    pick.clip_name = sLocoDecision.clip_name;

    tickCombatStance(combat_input_this_frame, tun);

    // Loco-freeze (layer 5): hold the previous loco clip when a
    // one-shot is staged-for-handoff and a swap now would corrupt
    // the BlendOut pose-match. Two conditions:
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
    pick.blend_seconds = sLocomotionConfig.blendInSeconds(pick.clip_name, pick.blend_seconds);
    // Capture pre-update joints AND emit the [sm loco-pick] log after
    // the final blend has been computed, so the log shows the
    // duration the sampler will actually use.
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
        combatLog("  [!] context: 1shot=%s(phase=%d) loco=%s\n",
                  fd.one_shot_name ? fd.one_shot_name : "(none)", fd.one_shot_phase,
                  fd.loco_current_name ? fd.loco_current_name : "(none)");
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
// Used for one-shot translation (the clip's authored hip motion
// drives world push during attacks, dodges, jumps). Locomotion
// uses applyVelocityToPlayerPos instead so the world-translation
// rate matches the input-driven velocity, not the clip's authored
// cadence (which would foot-skate at any tunable speed != the
// clip's authored speed). Delegates to the shared
// applyActorClipHipDelta — same math used by every other actor.
static void applyHipDeltaToPlayerPos()
{
    if (!sSampler.isOneShotActive())
        sOneShotHipDeltaScale = 1.0f;
    selva::gameplay::applyActorClipHipDelta(sPlayer, sOneShotHipDeltaScale);
}

// Translate the player by their current XZ velocity. Velocity is
// input-driven (acceleration / deceleration toward target speed in
// tickPlayerVelocity) so the world-push rate is independent of the
// loco clip's authored hip cadence. The clip's hip motion is ignored
// for locomotion — only velocity * dt lands on sPlayer.pos.
static void applyVelocityToPlayerPos(float dt)
{
    sPlayer.pos += glm::vec3(sPlayer.velocity_xz.x, 0.0f, sPlayer.velocity_xz.y) * dt;
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

// Per-frame translation routing. The active loco clip's
// Routes per active loco clip's translation_source (see
// feedback_hip_delta_two_sides). One-shots always root-motion;
// loco clips per JSON declaration (gait clips: root_motion;
// idles: velocity).
static void applyPerFrameTranslation(const glm::vec3& moveIntent, float dodge_steer_rate, float dt)
{
    if (!sDebugClipName.empty())
        return;
    if (sSampler.isOneShotActive())
    {
        if (sDodgeActive)
            applyDodgeSteer(moveIntent, dodge_steer_rate, dt);
        applyHipDeltaToPlayerPos();
        return;
    }
    if (sLocoDecision.source == selva::anim::TranslationSource::RootMotion)
    {
        applyHipDeltaToPlayerPos();
        return;
    }
    applyVelocityToPlayerPos(dt);
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

// Compute the WASD movement intent vector. Unlocked: camera-relative
// (forward = -Z rotated by cameraYaw). Locked: target-relative
// (forward = player→target on XZ). Locked mode anchors the basis to
// the target so W is always "toward target," not "into the camera."
static glm::vec3 computeMoveIntent(const Uint8* keys)
{
    glm::vec3 fwd;
    glm::vec3 right;
    if (const Actor* target = resolveLockTarget(); target != nullptr)
    {
        const glm::vec3 to(target->pos.x - sPlayer.pos.x, 0.0f, target->pos.z - sPlayer.pos.z);
        const float to_len_sq = glm::dot(to, to);
        if (to_len_sq <= 1e-6f)
            return glm::vec3(0.0f); // standing inside the target — no usable basis
        fwd = to / std::sqrt(to_len_sq);
        // Player-right axis: rotate fwd -90° around world-up so D
        // strafes to the player's right when facing the target.
        // Cross-checked vs camera-mode: yaw=0, camFwd=(0,0,-1),
        // camRight=(1,0,0); the same fwd here must yield the same
        // right.
        right = glm::vec3(-fwd.z, 0.0f, fwd.x);
    }
    else
    {
        fwd = glm::vec3(-std::sin(selva::render::cameraYaw()), 0.0f,
                        -std::cos(selva::render::cameraYaw()));
        right = glm::vec3(std::cos(selva::render::cameraYaw()), 0.0f,
                          -std::sin(selva::render::cameraYaw()));
    }
    glm::vec3 moveIntent(0.0f);
    if (keys[SDL_SCANCODE_W])
        moveIntent += fwd;
    if (keys[SDL_SCANCODE_S])
        moveIntent -= fwd;
    if (keys[SDL_SCANCODE_D])
        moveIntent += right;
    if (keys[SDL_SCANCODE_A])
        moveIntent -= right;
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
    bool press_r;
    bool shift_held;
};

// Read raw mouse + Shift + R state and compute the per-frame edges.
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
    const bool rNow = keys[SDL_SCANCODE_R] != 0;
    e.press_r = rNow && !sPrevR;
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
    if (tryHandleStanceTogglePress(e.press_r, e.shift_held))
        fired = true;

    const selva::combat::PressModifiers mods{e.shift_held, sPlayer.sprinting};
    if (tryAttackInputDispatch(selva::combat::HandSide::Right, e.press_lmb, "LMB", mods,
                               tun.combo_input_buffer_seconds))
        fired = true;

    const BlockClipSet block_set = resolveBlockClip(e.shift_held);
    if (block_set.raise != nullptr)
    {
        if (tickBlockingFromRMB(block_set, e.press_rmb, e.release_rmb,
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
    std::snprintf(sig, sizeof(sig), "p=%d|os=%s|loco=%s|d=%d|b=%d|bda=%d|fr=%d", fd.one_shot_phase,
                  fd.one_shot_name ? fd.one_shot_name : "",
                  fd.loco_current_name ? fd.loco_current_name : "", sDodgeActive ? 1 : 0,
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
    std::snprintf(buf, sizeof(buf), "loco=%s@%.2fs ", fd.loco_current_name, fd.loco_current_time);
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

// Apply one HitEvent: route to enemy or player, run applyDamage,
// spawn the damage number, fire the hit-react clip, log [hit].
// Extracted from selvaPerFrame to keep its CCN under the lint
// threshold; the corpse-skip continue + per-target branch had
// pushed it over.
static void applyHitEvent(const selva::combat::HitEvent& ev, float now)
{
    if (ev.target.kind == selva::combat::OwnerKind::Enemy)
    {
        auto enemy_view = selva::gameplay::enemies();
        if (ev.target.index < 0 || ev.target.index >= static_cast<int>(enemy_view.size()))
            return;
        auto& e = *enemy_view[ev.target.index];
        // Skip hits on dead/down enemies. Without this, HP gets
        // re-applied (clamped at 0), damage numbers spawn on
        // corpses, and [hit] logs fire after [enemy-death].
        // playEnemyHitReact has its own immunity gate but it runs
        // after this block.
        if (e.is_dead || e.is_knocked_down)
            return;
        const int hp_before = e.hp.current;
        selva::gameplay::applyDamage(e.hp, e.body, ev.raw_damage);
        const int dmg_applied = hp_before - e.hp.current;
        e.last_damage_time = now;
        const bool crit = (ev.region == selva::combat::HurtRegion::Head);
        const glm::vec3 number_origin(ev.world_pos.x, e.pos.y + 2.0f, ev.world_pos.z);
        selva::combat::spawnDamageNumber(number_origin, dmg_applied, crit);
        // Attacker pos is the player for player-faction hits. When
        // enemy-on-enemy comes online (e.g. friendly fire on factions
        // that hate their own kind), resolve from ev.attacker.kind.
        selva::gameplay::playEnemyHitReact(ev.target.index, dmg_applied, ev.poise_damage,
                                           ev.world_normal, sPlayer.pos);
        selva::combat::combatLog("[hit] enemy[%d] region=%d dmg=%d hp=%d/%d\n", ev.target.index,
                                 static_cast<int>(ev.region), dmg_applied, e.hp.current, e.hp.max);
        return;
    }
    if (ev.target.kind == selva::combat::OwnerKind::Player)
    {
        // Already-dead immunity: hits land on the corpse pose
        // (freeze_last from the second_death one-shot) but don't
        // re-damage or re-fire any reaction.
        if (sPlayer.is_dead)
            return;
        const int hp_before = sPlayer.hp.current;
        selva::gameplay::applyDamage(sPlayer.hp, sPlayer.body, ev.raw_damage);
        const int dmg_applied = hp_before - sPlayer.hp.current;
        const bool crit = (ev.region == selva::combat::HurtRegion::Head);
        const glm::vec3 number_origin(ev.world_pos.x, sPlayer.pos.y + 2.0f, ev.world_pos.z);
        selva::combat::spawnDamageNumber(number_origin, dmg_applied, crit);
        selva::combat::combatLog("[hit] player region=%d dmg=%d hp=%d/%d\n",
                                 static_cast<int>(ev.region), dmg_applied, sPlayer.hp.current,
                                 sPlayer.hp.max);
        // Death gate. The Vagrant is killed in Hell — sangue exits,
        // Hell's killing-protocol fires (visible suffering = the
        // second_death clip). Per setting.md *Second death*, the
        // soul has no imprint for Hell to grip, so the body dies and
        // the cosmology routes him back to the Wood (handled by
        // tickPlayerSecondDeathLifecycle below in selvaPerFrame).
        if (sPlayer.hp.current <= 0)
        {
            // Player is at index 0 in the pool. fireEnemyDeath is
            // actor-agnostic; reads sPlayer.death_clip_name = "second_death".
            selva::gameplay::fireEnemyDeath(sPlayer, /*index=*/0);
            return;
        }
        // Player hit-react. Don't interrupt the player's own swing —
        // that would feel awful; you'd lose committed offense to a
        // free enemy poke. Souls convention: hit-react fires only
        // when not already animating something heavier. Pick clip
        // by damage tier, same logic the enemy uses.
        if (!sSampler.isOneShotActive())
        {
            const auto& tun = selva::tuning::current();
            const char* clip_name = nullptr;
            float blend_in = 0.06f;
            float blend_out = 0.15f;
            if (static_cast<float>(dmg_applied) >= tun.hit_react_heavy_threshold)
            {
                clip_name = "hit_react_heavy";
                blend_in = 0.08f;
                blend_out = 0.20f;
            }
            else if (static_cast<float>(dmg_applied) >= tun.hit_react_medium_threshold)
            {
                clip_name = "hit_react_medium";
            }
            else
            {
                clip_name = "flinch_front"; // a light hit; directional pick deferred for player
            }
            const auto* clip = sClips.get(clip_name);
            if (clip != nullptr && clip->isLoaded())
            {
                selva::anim::PoseSampler::OneShotOptions opts;
                opts.clip_key = clip_name;
                sSampler.playOneShot(*clip, blend_in, blend_out,
                                     selva::anim::PoseSampler::BodyMask::Full,
                                     /*start_time_seconds=*/0.0f, /*playback_rate=*/1.0f, opts);
            }
        }
    }
}

// Player second-death lifecycle. Detects elapsed time since
// fireEnemyDeath(player) and respawns when the full clip-hold +
// card window has passed. Called once per frame from selvaPerFrame
// before any input/locomotion runs so the dead player's body is
// frozen by the second_death one-shot (freeze_last) for the full
// dead window, and on respawn the player is teleported to spawn_pos
// with full HP before locomotion/clip-pick reads its state.
//
// The card UI in ActorHud reads the same death_time + tunables to
// decide when to render — both halves of the lifecycle are driven
// off a single timestamp, no shared mutable flag.
// Pacing of the player second-death sequence. Clip-hold is the
// wallclock window between fireEnemyDeath(player) and the card
// appearing — long enough for the second_death clip to read. Card-
// seconds is how long the card lingers before respawn. Not Tunables-
// owned because the nlohmann macro is at its 62-field max; promote
// to runtime-tunable if/when content needs to adjust.
constexpr float kPlayerSecondDeathClipHoldSeconds = 3.5f;
constexpr float kPlayerSecondDeathCardSeconds = 4.0f;

static void tickPlayerSecondDeathLifecycle()
{
    if (!sPlayer.is_dead || sPlayer.death_time < 0.0f)
        return;
    const float elapsed = selva::wallClock() - sPlayer.death_time;
    const float total = kPlayerSecondDeathClipHoldSeconds + kPlayerSecondDeathCardSeconds;
    if (elapsed < total)
        return;

    selva::combat::combatLog("[player-respawn] respawning at t=%.3f\n", selva::wallClock());
    sPlayer.is_dead = false;
    sPlayer.death_time = -1.0f;
    sPlayer.last_damage_time = -1.0f;
    sPlayer.last_hit_react_time = -1.0f;
    sPlayer.pos = sPlayer.spawn_pos;
    sPlayer.yaw = sPlayer.spawn_yaw;
    sPlayer.velocity_xz = glm::vec2(0.0f);
    sPlayer.lock_target_idx = -1;
    selva::gameplay::initActorPools(sPlayer.hp, sPlayer.stamina, sPlayer.poise, sPlayer.body,
                                    sPlayer.stats);
    sSampler.releaseOneShot();
    // Unmuffle the OST — bookends the duck applied in fireEnemyDeath.
    selva::audio::restoreMusic();
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

    // Audio: drain the scheduled-SFX queue (peak-aligned plays, e.g.
    // the second-death synth firing so its peak lands on the card).
    selva::audio::tickScheduledSfx();

    // Player second-death lifecycle. Drives the dead->respawn timer
    // off the same death_time set by fireEnemyDeath(player). Runs
    // before any input/locomotion so a respawn this frame teleports
    // the player to spawn_pos before downstream systems read sPlayer.
    tickPlayerSecondDeathLifecycle();

    // Title bar — game name + FPS. EMA-smoothed so the number doesn't flicker.
    const int fps = static_cast<int>(std::lround(1.0 / engine.lastFrameTime()));
    engine.setWindowTitle("Selva Oscura  |  FPS " + std::to_string(fps));

    const auto& tun = selva::tuning::current();
    // Mirror tun.loco_playback_rate onto LocomotionConfig so the
    // sampler's advanceLocoTrack can read it without a Tunables
    // dependency. F1 panel writes tun; this line propagates.
    sLocomotionConfig.global_playback_rate = tun.loco_playback_rate;
    tickMouseLook(tun);

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

    // Dev: K kills the player instantly to test the second-death
    // lifecycle without taking real damage. Edge-triggered so holding
    // K doesn't re-fire each frame.
    {
        static bool sPrevK = false;
        const bool k_now = (keys[SDL_SCANCODE_K] != 0);
        if (k_now && !sPrevK && !sPlayer.is_dead)
        {
            sPlayer.hp.current = 0;
            selva::gameplay::fireEnemyDeath(sPlayer, /*index=*/0);
        }
        sPrevK = k_now;
    }

    tickF1TuningPanelToggle(keys);
    // Lock-on input gated while dead — the corpse can't acquire/release
    // a target. Camera still ticks so the lock-on framing remains
    // coherent if the death happened mid-engagement.
    if (!sPlayer.is_dead)
        tickLockOnInput();
    tickLockOnCamera();

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
    // Combat input gated on alive-state. While dead, the second_death
    // one-shot owns the body; player input is ignored until respawn.
    if (!sShowTuningPanel && !sPlayer.is_dead && tickCombatInputPass(keys, edges, tun))
        combat_input_this_frame = true;
    sPrevLMB = edges.lmb_now;
    sPrevRMB = edges.rmb_now;
    sPrevR = (keys[SDL_SCANCODE_R] != 0);

    selva::combat::cacheRightHandPos(sSampler);
    // Grip toggle + jump input are player-driven state changes; dead
    // gate so a corpse can't switch grip or initiate a jump. WASD-driven
    // moveIntent is computed unconditionally; the dead-gate downstream
    // (velocity_locked from the second_death one-shot) keeps it from
    // translating the body.
    if (!sPlayer.is_dead)
    {
        tickGripToggle(keys);
        tickJumpInput(keys);
    }

    const glm::vec3 moveIntent = computeMoveIntent(keys);

    // ONE locomotion decision for this frame. Both tickPlayerVelocity
    // and applyPerFrameTranslation consult sLocoDecision.source for
    // the bilateral translation contract; selectLocomotionClip later
    // turns sLocoDecision.clip_name into the actual LocomotionPick
    // (only applying the loco-freeze guard on top). See
    // feedback_data_driven_over_convention.md.
    runLocomotionDecision(moveIntent, tun);

    // Space (dodge / sprint) gated on alive-state. Dead body doesn't
    // dodge.
    if (!sPlayer.is_dead && tickSpaceInput(keys, moveIntent, dt, tun.dodge_tap_window,
                                           tun.backstep_playback_rate, tun.roll_playback_rate))
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
                             tun.combo_input_buffer_seconds))
        combat_input_this_frame = true;

    // Frame-capture timer (independent of dodge so post-dodge recovery
    // can also be captured). On expiry: emit contact-sheet composite.
    tickFrameCapture(dt);

    // Every active one-shot locks movement until BlendOut. The
    // whole body is committed to the clip — letting WASD push the
    // player would compound translation against the clip's
    // authored hip drive. WASD held during the lock is honored as
    // soon as the one-shot ends.
    //
    // Two separate locks:
    //   * movement_locked controls TRANSLATION source. While true,
    //     clip-hip drives sPlayer.pos (one-shot's authored arc).
    //     False → velocity * dt drives it. Stays locked for the
    //     full one-shot duration so we never double-translate.
    //   * velocity_locked controls the VELOCITY RAMP. While true,
    //     velocity_xz holds at zero. False → velocity ramps toward
    //     target speed even mid-one-shot. Unlocks past cancel_fraction
    //     so the player's velocity is already at target when the
    //     one-shot ends — no zero-velocity pop into idle, no
    //     foot-slide as residual clip-hip motion outpaces the
    //     ramp-from-zero locomotion.
    //
    // Result: jumps and dodges past their cancel_fraction have
    // velocity climbing during recovery. When the one-shot ends,
    // translation switches from clip-hip to velocity-driven without
    // a speed discontinuity. Attacks (cancel_fraction=1.0) never
    // unlock — full commitment preserved.
    const bool one_shot_active = sSampler.isOneShotActive();
    const bool past_cancel = sSampler.isOneShotPastCancelFraction();
    // Velocity also unlocks once the one-shot enters its BlendOut
    // phase (one_shot_phase == 3). This covers attacks (cancel_fraction
    // = 1.0 by design — never trips past_cancel) so velocity can ramp
    // up during the visible fade back to locomotion, instead of holding
    // at zero until the one-shot fully ends and then ramping from zero
    // while running animation plays.
    const int one_shot_phase = sSampler.frameDiagnostics().one_shot_phase;
    const bool in_blend_out = (one_shot_phase == 3);
    const bool movement_locked = sDodgeActive || one_shot_active;
    const bool velocity_locked =
        (sDodgeActive && !past_cancel) || (one_shot_active && !past_cancel && !in_blend_out);
    tickPlayerVelocity(moveIntent, velocity_locked, tun, dt);
    tickPlayerYaw(moveIntent, movement_locked, tun.turn_rate_min, tun.turn_rate_max, dt);

    // Pick + advance the sampler. The locomotion SM picks the loco
    // clip; F1 debug-clip preview overrides it. sampler.update runs
    // last so it sees post-input, post-movement state.
    const LocomotionPick pick = selectLocomotionClip(moveIntent, dt, combat_input_this_frame, tun);
    if (pick.clip != nullptr && pick.clip->isLoaded())
    {
        ZoneScopedN("sampler.update");
        sSampler.setActorPlacement(sPlayer.pos, sPlayer.yaw);
        sSampler.update(*pick.clip, dt, pick.blend_seconds, pick.loops, pick.clip_name.c_str());
    }

    logSpliceResiduals(pick.pre_update_joints, pick.clip);
    logBlendOutFadeJoints();

    applyPerFrameTranslation(moveIntent, tun.dodge_steer_rate, dt);

    // World collision: push player out of any static cylinder it
    // overlaps after translation. Runs LAST so it sees the final
    // post-translation position whatever drove it (velocity, clip-hip,
    // dodge steer). Player capsule radius ~0.35m matches humanoid
    // shoulder width. Enemy capsules push out the same way.
    {
        constexpr float kPlayerRadius = 0.35f;
        glm::vec2 player_xz(sPlayer.pos.x, sPlayer.pos.z);
        selva::world::resolveBodyCollision(player_xz, kPlayerRadius);
        for (const auto* enemy : selva::gameplay::enemies())
        {
            const glm::vec2 e_xz(enemy->pos.x, enemy->pos.z);
            const glm::vec2 delta = player_xz - e_xz;
            const float dist_sq = glm::dot(delta, delta);
            const float min_dist = kPlayerRadius + enemy->body.collider_radius;
            if (dist_sq >= min_dist * min_dist || dist_sq <= 1e-8f)
                continue;
            const float dist = std::sqrt(dist_sq);
            player_xz += (delta / dist) * (min_dist - dist);
        }
        sPlayer.pos.x = player_xz.x;
        sPlayer.pos.z = player_xz.y;
        // Snap to terrain after XZ resolves. Player feet sit on the
        // heightmap surface; the camera's lookAt-Y already smooths.
        sPlayer.pos.y = selva::world::sampleHeight(sPlayer.pos.x, sPlayer.pos.z);
    }

    selva::gameplay::tickEnemies(dt);

    // ---- Combat volume pipeline ----
    // Rebuild hurtboxes from current poses (cleared every frame —
    // bone positions move). Tick hitboxes (advance lifetime + roll
    // prev_shape for swept detection). Detect overlaps and apply
    // damage. Reactions/VFX read the same event stream.
    selva::combat::clearHurtboxes();
    {
        const glm::mat4 player_model = selva::combat::buildActorModelMatrix(
            sPlayer.pos, sPlayer.yaw, sPlayerMesh.foot_offset_y);
        selva::combat::appendActorHurtboxes(
            sSampler, player_model, sPlayer.body,
            selva::combat::OwnerRef{selva::combat::OwnerKind::Player, 0}, sPlayer.faction);
    }
    {
        const auto list = selva::gameplay::enemies();
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
        {
            const auto& e = *list[i];
            const glm::mat4 m =
                selva::combat::buildActorModelMatrix(e.pos, e.yaw, sPlayerMesh.foot_offset_y);
            selva::combat::appendActorHurtboxes(
                e.sampler, m, e.body, selva::combat::OwnerRef{selva::combat::OwnerKind::Enemy, i},
                e.faction);
        }
    }

    // Re-anchor every actor's active attack hitbox to the joint
    // driving it. Shared PC+NPC path — both attackers' hitboxes
    // track their swing arc this way. See Actor.cpp::
    // updateActiveAttackHitbox for the geometry; spawnAttackHitbox
    // records the joint_idx on the actor when the swing fires.
    for (auto& a : selva::gameplay::actors())
        selva::gameplay::updateActiveAttackHitbox(a);

    selva::combat::tickHitboxes(dt);
    const auto& hit_events = selva::combat::detectHits();
    const float now = selva::wallClock();
    for (const auto& ev : hit_events)
        applyHitEvent(ev, now);
    selva::combat::tickDamageNumbers(dt);

    tickCsvRecording(dt);
    logStateNarrative();
}

static void selvaRenderWorld(Engine& /*engine*/, EntityManager& /*em*/, float /*camX*/,
                             float /*camY*/, float /*alpha*/)
{
    ZoneScopedN("selvaRenderWorld");
    // LookAt height tracks the character's hip Y so dynamic poses
    // (rolls, knockdowns) keep the body in frame. The sampler's
    // jointWorldPos() returns the joint in MODEL-local space; add
    // the model's world-Y translation (= pos.y - foot_offset_y, the
    // same offset used when drawing the mesh) so the value is in
    // world space. Smoothed (relative to ground) in buildViewProj.
    float targetLookAtY = sPlayer.pos.y + 1.3f;
    if (sSampler.jointCount() > 0)
    {
        for (int i = 0; i < sSampler.jointCount(); ++i)
        {
            if (std::strcmp(sSampler.jointName(i), "mixamorig:Hips") == 0)
            {
                const float model_world_y = sPlayer.pos.y - sPlayerMesh.foot_offset_y;
                targetLookAtY = model_world_y + sSampler.jointWorldPos(i).y + 0.3f;
                break;
            }
        }
    }
    const glm::mat4 viewProj = selva::render::buildViewProj(sPlayer.pos, targetLookAtY);
    selva::render::setLastViewProj(viewProj);

    // Threshold-light direction toward the colle (-Z), held at deep
    // dusk elevation. Beatrice's light (per wood.md): no body, no
    // movement. Intensity well below "Earth sun" so the world reads
    // as held twilight rather than late afternoon. Slight warm tint
    // pre-mul on the source itself (the light is already mystical
    // before the atmosphere shapes it).
    constexpr glm::vec3 kSunDir(0.0f, 0.061f, -0.998f);
    constexpr glm::vec3 kSunIntensity(11.0f, 9.5f, 7.0f);
    constexpr float kExposure = 1.0f;

    // Camera position (matches buildViewProj math): player_pos - lookFwd
    // * follow_distance + (0, follow_height, 0). Reconstruct here.
    const auto& tun_atm = selva::tuning::current();
    const float camYaw = selva::render::cameraYaw();
    const float camPitch = selva::render::cameraPitch();
    const glm::vec3 lookFwd(std::cos(camPitch) * -std::sin(camYaw), std::sin(camPitch),
                            std::cos(camPitch) * -std::cos(camYaw));
    const glm::vec3 camPos = sPlayer.pos - lookFwd * tun_atm.follow_distance +
                             glm::vec3(0.0f, tun_atm.follow_height, 0.0f);

    selva::render::drawSky(glm::inverse(viewProj), kSunDir, kSunIntensity, camPos, kExposure);

    selva::render::useTerrainShader();
    selva::render::setTerrainViewProj(viewProj);
    selva::render::setTerrainAtmosphere(kSunDir, kSunIntensity, camPos, kExposure);
    selva::render::renderTerrain();

    selva::render::useSceneProgram();
    selva::render::setSceneView(selva::render::lastView());
    selva::render::setSceneViewProj(viewProj);
    selva::render::setSceneAtmosphere(kSunDir, kSunIntensity, camPos, kExposure);
    selva::render::renderGroundDecals();

    selva::render::useTreeShader();
    selva::render::setTreeViewProj(viewProj);
    selva::render::setTreeAtmosphere(kSunDir, kSunIntensity, camPos, kExposure);
    selva::render::renderTrees();

    glBindVertexArray(0);
    glUseProgram(0);

    // 3. Player — drawn as the rigged X Bot mesh, animated by the
    //    PoseSampler. Position comes from sPlayer.pos (X/Z); Y is
    //    -foot_offset_y so feet land on the floor regardless of where
    //    the rig's origin sits in bind pose. Yaw rotates the model
    //    around world-up to face the player's heading.
    if (sPlayerMesh.isLoaded() && !sSampler.bone_palette.empty())
    {
        const glm::vec3 player_pos(sPlayer.pos.x, sPlayer.pos.y - sPlayerMesh.foot_offset_y,
                                   sPlayer.pos.z);
        glm::mat4 player_model = glm::translate(glm::mat4(1.0f), player_pos);
        // Mixamo characters bind facing +Z. Our gameplay convention is
        // "yaw=0 means facing -Z" (matches camera-forward maths in
        // movement code). Add a 180° offset around world-up so
        // gameplay yaw 0 visually faces -Z while still lining up with
        // the bind-pose rig.
        player_model =
            glm::rotate(player_model, sPlayer.yaw + glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
        selva::anim::drawSkeletalMesh(sPlayerMesh, player_model, viewProj, sSampler.bone_palette,
                                      glm::vec3(1.0f, 1.0f, 1.0f));
    }

    // 4. Enemies — each is a separate PoseSampler instance on the same
    //    shared X_Bot rig (figura umana rule, see docs/design/bestiary.md).
    //    Slight tint shift so the player can tell them apart from
    //    placeholder cubes/trees while there's no material variation yet.
    if (sPlayerMesh.isLoaded())
    {
        for (const auto* enemy : selva::gameplay::enemies())
        {
            if (enemy->sampler.bone_palette.empty())
                continue;
            const glm::vec3 enemy_pos(enemy->pos.x, enemy->pos.y - sPlayerMesh.foot_offset_y,
                                      enemy->pos.z);
            glm::mat4 enemy_model = glm::translate(glm::mat4(1.0f), enemy_pos);
            enemy_model = glm::rotate(enemy_model, enemy->yaw + glm::pi<float>(),
                                      glm::vec3(0.0f, 1.0f, 0.0f));
            // Dark bordeaux red for the placeholder shade — distinct
            // silhouette from the player, hints at the figura-umana
            // damned-soul register (bloody / wretched).
            selva::anim::drawSkeletalMesh(sPlayerMesh, enemy_model, viewProj,
                                          enemy->sampler.bone_palette,
                                          glm::vec3(0.45f, 0.10f, 0.13f));
        }
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
