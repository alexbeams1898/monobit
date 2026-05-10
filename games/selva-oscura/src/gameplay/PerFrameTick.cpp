#include "gameplay/PerFrameTick.h"
#include "gameplay/TickState.h"

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
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <ozz/base/maths/soa_transform.h>
#include <string>
#include <vector>

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
using selva::combat::combatLog;
using selva::combat::isMovingLocoClip;
using selva::combat::SpliceDiag;
using selva::combat::isUnarmed;
using selva::combat::offHandCanBlock;
using selva::combat::effectiveAttackPlaybackRate;
using selva::combat::applyProfileLockout;
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

// AttackKind / AttackChainState / BufferedPress / PendingFirstAction +
// per-hand singletons + resetChain + tickChainExpiry live in
// combat/AttackChain.{h,cpp}. (Aliases declared in prelude above.)

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
static SpliceDiag captureSpliceDiag() { return selva::combat::captureSpliceDiag(sSampler); }
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
                                   float playback_rate)
{
    selva::combat::fireOneShotWithProfile(clip, profile, start_seconds, playback_rate, sSampler);
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

    // Mouse look. SDL_GetRelativeMouseState drains accumulated deltas.
    // Mouse-left rotates the camera left.
    int mdx = 0;
    int mdy = 0;
    SDL_GetRelativeMouseState(&mdx, &mdy);
    const auto& tun = selva::tuning::current();
    // Mouse-look only when the tuning panel is closed — otherwise the
    // mouse is being used to drag sliders, not to aim the camera.
    if (!sShowTuningPanel)
    {
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

    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Quit on Escape — handy until there's a pause menu.
    if (keys[SDL_SCANCODE_ESCAPE])
        engine.requestQuit();

    // F1: toggle the tuning panel (rising-edge detect). Releases the
    // cursor while the panel is open so ImGui can receive clicks; the
    // game's mouse-look pauses for that duration. Restores capture when
    // the panel closes.
    const bool f1Now = keys[SDL_SCANCODE_F1] != 0;
    if (f1Now && !sPrevF1)
    {
        sShowTuningPanel = !sShowTuningPanel;
        SDL_SetRelativeMouseMode(sShowTuningPanel ? SDL_FALSE : SDL_TRUE);
        // Drain accumulated relative motion so the camera doesn't snap
        // when capture is re-acquired.
        SDL_GetRelativeMouseState(nullptr, nullptr);
    }
    sPrevF1 = f1Now;

    // -------- Combat input --------
    //   LMB             — right-hand light attack
    //   RMB             — left-hand light attack  (player's left, viewer's right)
    //   Shift+LMB       — right-hand heavy
    //   Shift+RMB       — left-hand heavy
    //   Y (or G)        — toggle grip mode (one-handed ↔ two-handed)
    // Trigger on rising edge so each click is one swing; holding doesn't
    // spam. We don't gate on "is one-shot active" yet — pressing again
    // mid-swing cancels the previous one and starts a fresh blend-in
    // for the first attack of a chain.
    // Combos / queued follow-ups are a follow-up todo.
    const Uint32 mouse_buttons = SDL_GetMouseState(nullptr, nullptr);
    const bool lmbNow = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    const bool rmbNow = (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    const bool shift_held = (keys[SDL_SCANCODE_LSHIFT] != 0) || (keys[SDL_SCANCODE_RSHIFT] != 0);
    // Only fire when the tuning panel is closed — otherwise clicks meant
    // for ImGui sliders would also trigger swings.
    //
    // Attack-kind selection at swing time:
    //   * Sprinting + LMB/RMB → running attack (forward-lunge clip)
    //   * Shift + LMB/RMB     → heavy
    //   * Otherwise           → light
    // Sprint takes priority over Shift since "sprint-attack" is its own
    // distinct action; Shift+Sprint+LMB still triggers the running
    // attack. Every attack fires full-body and locks movement for its
    // duration; the SM resumes walking automatically when the one-shot
    // ends if WASD is still held.

    // Auto-reset aged chains + expire stale buffered presses. Lives in
    // combat/AttackChain.cpp.
    selva::combat::tickChainExpiry(selva::wallClock(), tun.combo_input_buffer_seconds);

    // ----- Combat fire path (rebuilt) -----
    //
    // Strike layer (this section): every press fires its mapped clip
    // fully — clip duration owns the timing. While a one-shot is in
    // flight, presses are buffered and replay when it ends. No chain
    // machinery here; the technique observer (combat/ChainObserver)
    // watches press history and reports matched techniques as state
    // for HUD / damage bonuses.
    //
    // Maps press button + modifiers (sprint, shift) to a clip name
    // via combat/PressMapping. Reads weapon class config; doesn't
    // know about "techniques" or "chains" or "finisher commits."

    // Fire a clip directly: pose-match against the current state,
    // pick a profile based on whether a one-shot is in flight, log
    // diag, and call playOneShot. No chain bookkeeping. Returns
    // true on successful fire.
    auto fireClip = [&](selva::combat::HandSide hand, const char* clip_name) -> bool
    {
        const auto* clip = sClips.get(clip_name);
        if (clip == nullptr || !clip->isLoaded())
            return false;
        const bool one_shot_active = sSampler.isOneShotActive();
        const auto fd_pre = sSampler.frameDiagnostics();

        // Pose-match: if a one-shot is interrupting another (chain
        // link), source = outgoing one-shot's current frame.
        // Otherwise source = live skinned pose (cold start).
        float pose_matched_start = -1.0f;
        if (one_shot_active && fd_pre.one_shot_name != nullptr)
        {
            const auto* prev_clip = sClips.get(fd_pre.one_shot_name);
            if (prev_clip != nullptr && prev_clip->isLoaded())
            {
                const int rh = sSampler.findJoint("mixamorig:RightHand");
                const int lh = sSampler.findJoint("mixamorig:LeftHand");
                std::vector<int> joints;
                if (rh >= 0)
                    joints.push_back(rh);
                if (lh >= 0)
                    joints.push_back(lh);
                if (!joints.empty())
                    pose_matched_start = sSampler.clipPoseMatchTime(
                        *prev_clip, fd_pre.one_shot_time, *clip, joints, 0.0f, 0.50f);
            }
        }
        else
        {
            pose_matched_start = poseMatchStartFromLoco(*clip, 0.30f);
        }
        const float start_seconds = (pose_matched_start >= 0.0f) ? pose_matched_start : 0.0f;

        TransitionProfile profile = one_shot_active ? profiles::chainLink() : profiles::firstStrike();
        // No locomotion lockout — every clip plays fully via buffer
        // logic; no need to gate movement separately.
        profile.lockout = TransitionProfile::Lockout::None;
        // Per-attack blend_out override. Finds the WeaponAttack with
        // this clip in the equipped weapon's techniques. If found and
        // the attack specifies blend_out_seconds >= 0, use it. This
        // lets the unarmed combo finisher (waddle-tail) trim its
        // visible recovery without affecting jab/hook.
        if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr)
        {
            const auto& aset = (sEquipment.grip == selva::combat::Grip::TwoHanded)
                                   ? sEquipment.right->cls->two_handed
                                   : sEquipment.right->cls->one_handed;
            const std::vector<const std::vector<selva::combat::WeaponTechnique>*> tech_lists{
                &aset.light, &aset.heavy, &aset.running};
            for (const auto* techs : tech_lists)
            {
                for (const auto& tech : *techs)
                {
                    for (const auto& atk : tech.attacks)
                    {
                        if (atk.clip == clip_name && atk.blend_out_seconds >= 0.0f)
                        {
                            profile.blend_out_seconds = atk.blend_out_seconds;
                            goto blend_out_resolved;
                        }
                    }
                }
            }
        blend_out_resolved:;
        }
        const float rate = effectiveAttackPlaybackRate(hand);
        fireOneShotWithProfile(*clip, profile, start_seconds, rate);

        // Set the per-hand cancel window in the observer. clipForButton
        // gates chain advancement on this; the HUD renders the green
        // band from it.
        const float cancel_open = (clip->duration() > 0.0f) ? clip->duration() * 0.40f : 0.30f;
        const float cancel_close = cancel_open + tun.combo_input_buffer_seconds;
        selva::combat::setCancelWindow(hand, selva::wallClock() + cancel_open / rate,
                                       selva::wallClock() + cancel_close / rate);

        combatLog("[combat:fire] hand=%s clip=%s dur=%.3fs start=%.3fs rate=%.2f one_shot_active=%d\n",
                  (hand == selva::combat::HandSide::Right) ? "R" : "L", clip_name, clip->duration(),
                  start_seconds, rate, one_shot_active ? 1 : 0);
        return true;
    };

    // Try a press: if no one-shot is active, fire immediately.
    // Otherwise buffer for replay after the in-flight clip ends.
    // This is the entirety of the press-handler logic.
    auto tryAttackInput = [&](selva::combat::HandSide hand, bool press_edge_this_frame,
                               const char* button)
    {
        BufferedPress& buf =
            (hand == selva::combat::HandSide::Right) ? sBufferedRight : sBufferedLeft;

        const selva::combat::PressModifiers mods{shift_held, sPlayer.sprinting};

        // Fresh press during dodge: buffer for post-dodge handoff.
        if (press_edge_this_frame && sDodgeActive)
        {
            sPostDodgeAttack.pending = true;
            sPostDodgeAttack.hand = hand;
            sPostDodgeAttack.button = button;
            sPostDodgeAttack.buffered_at = selva::wallClock();
            combatLog("[combat:rhythm %.4fs] press BUFFERED (dodge active)\n", selva::wallClock());
            return;
        }

        auto fire_and_record = [&](const char* button_to_fire, float now)
        {
            const auto& w = selva::combat::cancelWindow(hand);
            const float center = 0.5f * (w.open_at + w.close_at);
            const float half = 0.5f * (w.close_at - w.open_at);
            selva::combat::recordPress(sEquipment, button_to_fire, now, center, half);
        };

        if (press_edge_this_frame)
        {
            const float now = selva::wallClock();
            if (sSampler.isOneShotActive())
            {
                // Press while a one-shot is in flight. ONLY fire
                // immediately if it advances a chain (LMB→RMB→LMB
                // inside windows = jab→hook→combo finisher). Wrong
                // button / no chain / outside window → buffer for
                // replay after the clip finishes.
                const auto& w = selva::combat::cancelWindow(hand);
                const bool inside_window =
                    w.close_at > w.open_at && now >= w.open_at && now <= w.close_at;
                if (inside_window)
                {
                    bool is_chain_advance = false;
                    const char* clip_name = selva::combat::clipForButton(
                        sEquipment, hand, button, mods, now, w.open_at, w.close_at,
                        &is_chain_advance);
                    if (is_chain_advance && clip_name != nullptr && fireClip(hand, clip_name))
                    {
                        combat_input_this_frame = true;
                        fire_and_record(button, now);
                        return;
                    }
                }
                buf.pending = true;
                buf.button = button;
                buf.buffered_at = now;
                combatLog("[combat:rhythm] BUFFERED (one-shot in flight, no chain advance; %s)\n",
                          button);
                return;
            }
            // No one-shot active: fire mapped clip directly.
            const auto& w = selva::combat::cancelWindow(hand);
            const char* clip_name = selva::combat::clipForButton(
                sEquipment, hand, button, mods, now, w.open_at, w.close_at);
            if (clip_name == nullptr)
            {
                combatLog("[combat:rhythm] press DROPPED (no clip mapping for %s)\n", button);
                return;
            }
            if (fireClip(hand, clip_name))
            {
                combat_input_this_frame = true;
                fire_and_record(button, now);
            }
            return;
        }

        // No fresh press: replay buffered if one-shot just ended.
        if (buf.pending && !sSampler.isOneShotActive())
        {
            const float now = selva::wallClock();
            if (now - buf.buffered_at > tun.combo_input_buffer_seconds)
            {
                buf.pending = false;
                return;
            }
            const auto& w = selva::combat::cancelWindow(hand);
            const char* clip_name = selva::combat::clipForButton(
                sEquipment, hand, buf.button, mods, now, w.open_at, w.close_at);
            if (clip_name != nullptr && fireClip(hand, clip_name))
            {
                combat_input_this_frame = true;
                fire_and_record(buf.button, now);
            }
            buf.pending = false;
        }
    };

    // Tick the technique observer: clears history if the player has
    // gone quiet for combo_reset_grace_seconds. Independent of fires.
    selva::combat::tickChainObserver(selva::wallClock(), sSampler.isOneShotActive());

    if (!sShowTuningPanel)
    {
        const bool press_lmb = lmbNow && !sPrevLMB;
        const bool press_rmb = rmbNow && !sPrevRMB;
        const bool release_rmb = !rmbNow && sPrevRMB;
        const bool release_lmb = !lmbNow && sPrevLMB;
        const bool fNow = keys[SDL_SCANCODE_F] != 0;
        const bool press_f = fNow && !sPrevF;

        // High-resolution input edge trace. Logs every WASD/LMB/RMB
        // transition with the wall-clock timestamp so the user's repro
        // session can be reconstructed exactly. Gated on debug switch.
        if (selva::combat::isCombatDebugEnabled())
        {
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

        // F: explicit combat-stance toggle.
        //   * F (Peaceful)        → enter CombatReady. Latches the
        //     stance the same way an attack does — combat_input_this_
        //     frame=true makes the SM crossfade locomotion to combat
        //     idle and push stance_active_until.
        //   * F (CombatReady)     → drop to Peaceful immediately.
        //     Zeroes stance_active_until so the SM tick exits stance.
        //   * Shift+F (CombatReady) → flip stance foot. No-op visually
        //     today (clip selection doesn't branch on foot yet); the
        //     slot is wired so a mirror-moveset addition slots in
        //     without changing this control surface.
        if (press_f)
        {
            if (sLocomotionSM.combat_stance == CombatStance::Peaceful)
            {
                combat_input_this_frame = true; // SM enters CombatReady
            }
            else
            {
                if (shift_held)
                {
                    sLocomotionSM.stance_foot =
                        (sLocomotionSM.stance_foot == CombatStanceFoot::Default)
                            ? CombatStanceFoot::Mirror
                            : CombatStanceFoot::Default;
                }
                else
                {
                    sLocomotionSM.stance_active_until = 0.0f;
                    sLocomotionSM.combat_stance = CombatStance::Peaceful;
                }
            }
        }

        // LMB → right hand action (semantic mapping is on the future
        // todo when stance flip lands; for now LMB=right, RMB=left).
        tryAttackInput(selva::combat::HandSide::Right, press_lmb, "LMB");

        // RMB routing: if the off-hand is a blocker (buckler-class),
        // hold-RMB enters the held-block state. Otherwise, RMB tap
        // fires an off-hand attack chain like LMB.
        //
        // loco_settled = true when fired via the first-action latch
        // (locomotion already crossfaded to combat-idle); false when
        // fired live, in which case we snap the loco track to t=0
        // before splicing.
        auto fireBlock = [&](bool loco_settled, const char* clip_name, bool freeze_last)
        {
            const auto* clip = sClips.get(clip_name);
            if (clip != nullptr && clip->isLoaded())
            {
                const int rh = sSampler.findJoint("mixamorig:RightHand");
                const auto fd_pre = sSampler.frameDiagnostics();
                const glm::vec3 live_pre = (rh >= 0) ? sSampler.jointWorldPos(rh) : glm::vec3(0);
                // Trust the weapon class's leading-idle trim if set;
                // otherwise pose-match against the live loco track.
                float block_start = 0.0f;
                if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr &&
                    sEquipment.right->cls->block_clip_start_seconds > 0.0f)
                    block_start = sEquipment.right->cls->block_clip_start_seconds;
                else
                    block_start = poseMatchStartFromLoco(*clip, 0.30f);
                TransitionProfile profile =
                    loco_settled ? profiles::blockFromLatch() : profiles::blockLive();
                profile.freeze_last = freeze_last;
                fireOneShotWithProfile(*clip, profile, block_start, /*playback_rate=*/1.0f);
                sBlockingActive = true;

                const glm::vec3 block_t0 =
                    (rh >= 0) ? sSampler.sampleJointWorldPos(*clip, 0.0f, rh) : glm::vec3(0);
                const glm::vec3 d = block_t0 - live_pre;
                combatLog("[combat:block-fire] mode=%s  loco=%s clip_t=%.3fs  "
                          "RH live=(%.3f,%.3f,%.3f)  block_t0=(%.3f,%.3f,%.3f)  delta=|%.3fm|\n",
                          loco_settled ? "SETTLED" : "SNAP",
                          fd_pre.loco_current_name ? fd_pre.loco_current_name : "(none)",
                          fd_pre.loco_current_time, live_pre.x, live_pre.y, live_pre.z,
                          block_t0.x, block_t0.y, block_t0.z, glm::length(d));
            }
        };

        // Resolve the block intent for this RMB event:
        //   - Buckler equipped: RMB always blocks (sword_and_shield_block).
        //   - Unarmed + Shift held: blocks (unarmed_block, freeze_last).
        //   - Otherwise: not a block; falls through to attack chain.
        const char* block_clip_name = nullptr;
        bool block_freeze_last = false;
        if (offHandCanBlock(sEquipment))
            block_clip_name = "sword_and_shield_block";
        else if (isUnarmed(sEquipment) && shift_held)
        {
            block_clip_name = "unarmed_block";
            block_freeze_last = true;
        }

        if (block_clip_name != nullptr)
        {
            if (press_rmb)
            {
                const bool from_peaceful =
                    (sLocomotionSM.combat_stance == CombatStance::Peaceful);
                if (from_peaceful && !sPendingFirstAction.active)
                {
                    sPendingFirstAction.active = true;
                    sPendingFirstAction.block_clip = block_clip_name;
                    sPendingFirstAction.block_freeze_last = block_freeze_last;
                    sPendingFirstAction.fire_at =
                        selva::wallClock() + tun.combat_entry_delay_seconds;
                    combat_input_this_frame = true;
                }
                else if (!sPendingFirstAction.active)
                {
                    fireBlock(false, block_clip_name, block_freeze_last);
                    combat_input_this_frame = true;
                }
            }
            else if (release_rmb && sBlockingActive)
            {
                sBlockingActive = false;
                sSampler.releaseOneShot();
                combat_input_this_frame = true;
            }
        }
        else
        {
            // Off-hand can't block, no shift+unarmed gesture. Route RMB
            // into the same chain as LMB (sChainRight) so both buttons
            // advance one shared combo. The technique selected (jab→hook→
            // combo Chain A vs Chain B) depends on the button sequence.
            tryAttackInput(selva::combat::HandSide::Right, press_rmb, "RMB");
        }

        // Block-latch: block fired from Peaceful waits combat_entry_delay
        // for the loco track to settle. Attack latch was removed in the
        // combat rebuild — attacks fire immediately via tryAttackInput.
        if (sPendingFirstAction.active && selva::wallClock() >= sPendingFirstAction.fire_at)
        {
            fireBlock(true, sPendingFirstAction.block_clip,
                      sPendingFirstAction.block_freeze_last);
            combat_input_this_frame = true;
            sPendingFirstAction.active = false;
        }
    }
    sPrevLMB = lmbNow;
    sPrevRMB = rmbNow;
    sPrevF = (keys[SDL_SCANCODE_F] != 0);

    // Cache this frame's right-hand position for next frame's splice
    // diagnostic — used to compute pre-splice velocity.
    selva::combat::cacheRightHandPos(sSampler);

    // Grip toggle (Y or G — both on, since Y is right-hand-near and G is
    // a comfortable WASD reach). Toggle on rising edge.
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

    // Compute camera-relative movement intent every frame; both walking and
    // dodge-init read it. Forward axis is -Z rotated by camera yaw; right
    // axis is perpendicular in the XZ plane.
    glm::vec3 moveIntent(0.0f);
    const glm::vec3 camFwd(-std::sin(selva::render::cameraYaw()), 0.0f, -std::cos(selva::render::cameraYaw()));
    const glm::vec3 camRight(std::cos(selva::render::cameraYaw()), 0.0f, -std::sin(selva::render::cameraYaw()));
    if (keys[SDL_SCANCODE_W])
        moveIntent += camFwd;
    if (keys[SDL_SCANCODE_S])
        moveIntent -= camFwd;
    if (keys[SDL_SCANCODE_D])
        moveIntent += camRight;
    if (keys[SDL_SCANCODE_A])
        moveIntent -= camRight;

    // Space input — tap-vs-hold disambiguation.
    //   * Tap (released within tun.dodge_tap_window) → dodge roll.
    //   * Hold past the window                       → sprint engages.
    //   * Release while sprinting                    → sprint stops, no dodge.
    //   * Release after dodge already fired          → no second action.
    //
    // Implementation:
    //   - On press (rising edge): start the timer, clear the
    //     "dodge fired" flag. Don't engage sprint yet — we don't know
    //     if this is a tap or a hold.
    //   - While held: increment timer. Once it crosses the window,
    //     engage sprint (commits to hold semantics).
    //   - On release (falling edge): if the timer is still under the
    //     window AND the dodge hasn't already fired AND we aren't
    //     already mid-dodge, fire the dodge. Otherwise just clear
    //     sprint (we're past the window or already dodging).
    {
        const bool space_now = keys[SDL_SCANCODE_SPACE] != 0;
        const bool press_edge = space_now && !sPrevSpace;
        const bool release_edge = !space_now && sPrevSpace;

        if (press_edge)
        {
            sSpaceHeldSeconds = 0.0f;
            sDodgeFiredThisPress = false;
            // Don't change sPlayer.sprinting on press — wait until the
            // hold-window elapses.
        }
        if (space_now)
        {
            sSpaceHeldSeconds += dt;
            if (sSpaceHeldSeconds >= tun.dodge_tap_window)
                sPlayer.sprinting = true;
        }
        if (release_edge)
        {
            const bool was_tap = sSpaceHeldSeconds < tun.dodge_tap_window;
            // Symmetric to fireAttack's dodge guard: an attack one-
            // shot also commits — dodge can't cancel an in-flight
            // attack. Same splice math: rolling out of mid-jab pose
            // produces a huge offset and visible spasm.
            const bool attack_active = sSampler.isOneShotActive() && !sDodgeActive;
            if (was_tap && !sDodgeFiredThisPress && !sDodgeActive && !attack_active)
            {
                // Fire the dodge. WASD held → directional roll
                // (falling_to_roll); no WASD → backstep
                // (standing_dodge_backward). Both clips have monotonic
                // hip XZ travel (verified via auditClipHipMotion's
                // trajectory dump): rotated by sPlayer.yaw, the
                // clip-local hip motion lands as world-frame travel
                // in the direction the player faces.
                //
                // We use falling_to_roll instead of stand_to_roll
                // because stand_to_roll's hip ping-pongs back to origin
                // (3.37m path, ~0m net) — visually rolls but authors
                // no net travel, so root motion gives "rolling in
                // place." falling_to_roll is monotonic +Z travel
                // (3.01m net over 1.47s) and works as a true root-
                // motion source.
                const bool has_intent = glm::length(moveIntent) > 0.0001f;
                // TEMP: bookends dropped for A/B test. User wants to
                // feel raw splice quality before deciding the commit
                // baseline for the architectural rework.
                const char* clip_name = has_intent ? "falling_to_roll" : "standing_dodge_backward";
                sDodgeIsBackstep = !has_intent;
                if (has_intent)
                {
                    const glm::vec3 dir = glm::normalize(moveIntent);
                    sPlayer.yaw = yawFromGroundDir(dir);
                }
                const auto* dodgeClip = sClips.get(clip_name);
                if (dodgeClip != nullptr && dodgeClip->isLoaded())
                {
                    const float playback_rate =
                        sDodgeIsBackstep ? tun.backstep_playback_rate : tun.roll_playback_rate;
                    SpliceDiag diag;
                    if (selva::combat::isCombatDebugEnabled())
                        diag = captureSpliceDiag();
                    fireOneShotWithProfile(*dodgeClip, profiles::dodge(), 0.0f, playback_rate);
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
                    // sDodgeDuration is the WALL-CLOCK time the
                    // dodge-active gate stays open. With root motion,
                    // the clip's authored hip motion drives world
                    // translation directly — when the clip ends, the
                    // travel ends. We just need a wall-clock value to
                    // gate the input lock and the post-dodge tail
                    // logic, so dur / rate gives us that.
                    sDodgePlaybackRate = std::max(0.1f, playback_rate);
                    sDodgeDuration = dodgeClip->duration() / sDodgePlaybackRate;
                    sDodgeFiredThisPress = true;
                    combat_input_this_frame = true;

                    // CSV recording: if armed via the F1 panel, capture
                    // this dodge's bone trajectories for diffing against
                    // the F1-clip-debug recording. Output filename is
                    // prefixed `dodge_` so the two recordings of the
                    // same clip don't clobber each other.
                    if (sDebugRecordArmedNextDodge && !sDebugRecording)
                    {
                        sDebugRecording = true;
                        sDebugRecordElapsed = 0.0f;
                        sDebugRecordDuration = sDodgeDuration;
                        sDebugRecordClipName = "dodge_stand_to_roll";
                        sDebugRecordSamples.clear();
                        sDebugRecordSamples.reserve(
                            static_cast<std::size_t>(sDodgeDuration * 65.0f));
                        sDebugRecordArmedNextDodge = false;
                        std::fprintf(stderr, "[anim-debug] recording dodge for %.2fs\n",
                                     sDodgeDuration);
                    }
                    // Frame-capture arm: record PNG per frame for dodge
                    // duration. Output dir is build/bin/frame_capture/.
                    // We don't clean it between runs; user/agent does.
                    if (sFrameCaptureArmedNextDodge && !sFrameCaptureActive)
                    {
                        sFrameCaptureDir = "frame_capture";
                        std::error_code ec;
                        std::filesystem::create_directories(sFrameCaptureDir, ec);
                        sFrameCaptureActive = true;
                        sFrameCaptureCounter = 0;
                        sFrameCaptureElapsed = 0.0f;
                        // Capture 0.3s past the dodge end so we see the
                        // recovery-back-to-idle blend.
                        sFrameCaptureDuration = sDodgeDuration + 0.3f;
                        sFrameCaptureArmedNextDodge = false;
                        std::fprintf(stderr, "[frame-capture] capturing dodge for %.2fs to %s/\n",
                                     sFrameCaptureDuration, sFrameCaptureDir.c_str());
                    }
                }
            }
            // Either way, dropping Space drops sprint.
            sPlayer.sprinting = false;
        }
        sPrevSpace = space_now;
    }

    // Tick the dodge timer. The active gate ends at the clip's full
    // duration. With root motion, the clip's authored hip motion goes
    // to ~zero during the standup tail, so additional translation
    // naturally tapers — there's no "post-landing slide" that the
    // legacy script-push had, which is why we no longer need a
    // separate motion_end cutoff.
    if (sDodgeActive)
    {
        sDodgeElapsed += dt;
        if (sDodgeElapsed >= sDodgeDuration)
        {
            sDodgeActive = false;
            if (selva::combat::isCombatDebugEnabled())
                combatLog("[combat:dodge %.4fs] sDodgeActive=false (elapsed=%.3fs/%.3fs)\n",
                          selva::wallClock(), sDodgeElapsed, sDodgeDuration);
        }
    }
    // One-shot state change tracking for post-dodge handoff visibility.
    {
        static bool sPrevOneShotActive = false;
        const bool one_shot_active_now = sSampler.isOneShotActive();
        if (sPrevOneShotActive != one_shot_active_now && selva::combat::isCombatDebugEnabled())
        {
            const auto fd = sSampler.frameDiagnostics();
            combatLog("[combat:one-shot %.4fs] active=%d weight=%.3f phase=%d\n",
                      selva::wallClock(), one_shot_active_now ? 1 : 0, fd.one_shot_weight,
                      fd.one_shot_phase);
        }
        sPrevOneShotActive = one_shot_active_now;
    }

    // Post-dodge attack handoff. Once the dodge's gate has closed AND
    // the one-shot is fully done blending out, fire the buffered
    // attack as a normal first-strike. Combat-idle is now showing on
    // screen (one-shot ended), so the splice is small (per-joint
    // decay handles whatever's left). This is the "chain LMB into
    // the roll" UX — the press lands during the dodge, fires on a
    // clean handoff frame, no spasm.
    // Post-dodge attack handoff. Wait for the dodge clip to fully end
    // AND for the one-shot to fully blend out before firing. The
    // earlier "cancel mid-roll at 65%" experiment fired the attack
    // when the player was tucked mid-dive, producing 89cm hand and
    // 47cm foot offsets at the splice (visible full-body spasm).
    // The diagnostic in combat-debug.log made this concrete: the
    // mid-roll pose IS catastrophically far from the jab's authored
    // t=0 pose. Clean splice requires waiting for the roll to settle
    // back to combat-idle pose first.
    if (sPostDodgeAttack.pending && !sDodgeActive && !sSampler.isOneShotActive())
    {
        const float wait_seconds = selva::wallClock() - sPostDodgeAttack.buffered_at;
        const selva::combat::PressModifiers mods{shift_held, sPlayer.sprinting};
        const auto& dw = selva::combat::cancelWindow(sPostDodgeAttack.hand);
        const char* clip_name = selva::combat::clipForButton(
            sEquipment, sPostDodgeAttack.hand, sPostDodgeAttack.button, mods,
            selva::wallClock(), dw.open_at, dw.close_at);
        if (clip_name != nullptr && fireClip(sPostDodgeAttack.hand, clip_name))
        {
            combat_input_this_frame = true;
            combatLog("[combat:rhythm %.4fs] post-dodge attack FIRED (waited %.3fs since press)\n",
                      selva::wallClock(), wait_seconds);
        }
        sPostDodgeAttack.pending = false;
    }

    // Frame-capture timer. Independent of sDodgeActive so we can also
    // capture the post-dodge recovery into idle (sFrameCaptureDuration
    // is set to dodge_duration + 0.3s at arm time).
    if (sFrameCaptureActive)
    {
        sFrameCaptureElapsed += dt;
        if (sFrameCaptureElapsed >= sFrameCaptureDuration)
        {
            std::fprintf(stderr, "[frame-capture] wrote %d frames to %s/\n", sFrameCaptureCounter,
                         sFrameCaptureDir.c_str());

            // Build a contact-sheet composite: all captured frames in
            // a grid, one wide PNG. Reading the strip in a single Read
            // call gives a holistic view of the dodge progression
            // instead of the agent eyeballing 140 individual PNGs in
            // sequence and losing context between them.
            //
            // Layout: square-ish grid. We pick `cols` so the grid is
            // close to 16:9 aspect for readability at default zoom.
            const int n_frames = sFrameCaptureCounter;
            if (n_frames > 0)
            {
                // Stride captured frames so the contact sheet has at
                // most kMaxCells. A 150-frame chain capture at full
                // 640x360 cells with 6 cols and 26 rows would allocate
                // ~100MB on the main thread and write a ~10000-tall
                // PNG, which crashes mid-game when triggered live. We
                // sample every Nth frame to cap the sheet's memory and
                // keep the per-cell resolution readable. Individual
                // PNG frames remain unstrided on disk — only the
                // contact sheet uses the strided subset.
                // Tighter caps so the contact sheet is actually readable
                // when Read'd by the agent (Read shrinks to ~600px wide,
                // so 6 cols × 100px/cell = unreadable). 3 cols × 200px
                // each is the sweet spot for visible per-frame motion.
                constexpr int kMaxCells = 30;
                const int stride = std::max(1, (n_frames + kMaxCells - 1) / kMaxCells);
                const int n_cells = (n_frames + stride - 1) / stride;
                constexpr int kMaxCols = 3;
                int cols = std::min(
                    kMaxCols, std::max(1, static_cast<int>(std::ceil(std::sqrt(
                                              static_cast<float>(n_cells) * 1.78f)))));
                int rows = (n_cells + cols - 1) / cols;
                // Read frame 0 to get the per-cell dimensions.
                char path0[512];
                std::snprintf(path0, sizeof(path0), "%s/frame_0000.png", sFrameCaptureDir.c_str());
                int cell_w = 0;
                int cell_h = 0;
                int cell_ch = 0;
                stbi_uc* probe = stbi_load(path0, &cell_w, &cell_h, &cell_ch, 3);
                if (probe != nullptr && cell_w > 0 && cell_h > 0)
                {
                    stbi_image_free(probe);
                    constexpr int kBorder = 2;
                    const int sheet_w = cols * (cell_w + kBorder) + kBorder;
                    const int sheet_h = rows * (cell_h + kBorder) + kBorder;
                    // Sanity-cap at ~64MB. If we'd exceed this, skip the
                    // contact sheet entirely (individual PNGs already on
                    // disk) and log a clear notice rather than crashing.
                    const std::size_t bytes =
                        static_cast<std::size_t>(sheet_w) * sheet_h * 3;
                    if (bytes > 64ull * 1024 * 1024)
                    {
                        std::fprintf(stderr,
                                     "[frame-capture] sheet would be %zu MB (%dx%d %dx%d cells); "
                                     "skipping. Individual PNGs at %s/\n",
                                     bytes / (1024 * 1024), sheet_w, sheet_h, cols, rows,
                                     sFrameCaptureDir.c_str());
                    }
                    else
                    {
                        std::vector<unsigned char> sheet(bytes, 32);
                        int cell_idx = 0;
                        for (int i = 0; i < n_frames; i += stride)
                        {
                            char fp[512];
                            std::snprintf(fp, sizeof(fp), "%s/frame_%04d.png",
                                          sFrameCaptureDir.c_str(), i);
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
                                const int dst_off = ((y0 + y) * sheet_w + x0) * 3;
                                const int src_off = y * cell_w * 3;
                                std::memcpy(&sheet[dst_off], &img[src_off], cell_w * 3);
                            }
                            stbi_image_free(img);
                            ++cell_idx;
                        }
                        char sheet_path[512];
                        std::snprintf(sheet_path, sizeof(sheet_path), "%s/_contact_sheet.png",
                                      sFrameCaptureDir.c_str());
                        stbi_write_png(sheet_path, sheet_w, sheet_h, 3, sheet.data(),
                                       sheet_w * 3);
                        std::fprintf(
                            stderr,
                            "[frame-capture] contact sheet: %s (%dx%d, %d cells from %d "
                            "frames stride=%d, %dx%d grid)\n",
                            sheet_path, sheet_w, sheet_h, n_cells, n_frames, stride, cols, rows);
                    }
                }
            }

            sFrameCaptureActive = false;
        }
    }

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
    if (!movement_locked && glm::length(moveIntent) > 0.0001f)
    {
        const glm::vec3 dir = glm::normalize(moveIntent);
        const float targetYaw = yawFromGroundDir(dir);
        float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
        const float maxStep = tun.turn_rate * dt;
        if (delta > maxStep)
            delta = maxStep;
        else if (delta < -maxStep)
            delta = -maxStep;
        sPlayer.yaw += delta;
    }

    // Pick + advance the sampler. selectClipForPlayer chooses the clip
    // for this frame based on locomotion + sprint state; the sampler
    // cross-fades on its own when the choice changes. Done last so it
    // sees post-input, post-movement state.
    //
    // Debug override: if the F1 panel has selected a clip to preview in
    // isolation, route THAT clip into the sampler instead. Skips the
    // gameplay state machine entirely, with no crossfade and no one-shot
    // blending — pure single-clip playback. Used to side-by-side compare
    // a clip's appearance in-game against the browser previewer.
    //
    // For gameplay clips, the per-clip blend duration comes from
    // locomotion.json (e.g. Run → Idle settles slowly, Idle → Run snaps
    // fast). Falls back to tun.anim_blend_seconds for any clip without
    // an entry, including the debug-override path.
    std::string clip_name;
    const selva::anim::AnimationClip* clip = nullptr;
    bool clip_loops = true;
    float clip_blend_seconds = tun.anim_blend_seconds;
    struct PreUpdateJoint { const char* name; int idx; glm::vec3 live; };
    std::vector<PreUpdateJoint> pre_update_joints;
    if (!sDebugClipName.empty())
    {
        // F1 debug-clip preview: bypass the state machine entirely.
        clip_name = sDebugClipName;
        clip = sClips.get(clip_name);
        clip_loops = true; // debug previews loop
        clip_blend_seconds =
            sLocomotionConfig.blendInSeconds(clip_name, tun.anim_blend_seconds);
    }
    if (clip == nullptr)
    {
        // Drive locomotion through the state machine so transitions
        // pick up the right clips (start_walking, run_to_stop, etc.)
        // and so phase-matched crossfades only fire between gait-cycle
        // peers (walk ↔ run).
        //
        // Suppress is_moving while an attack is in flight OR within
        // the post-attack recovery window. Without the recovery
        // suppression, WASD smashing the moment the one-shot ends
        // can ping-pong walking ↔ idle multiple times per second and
        // produce visible leg spasms — even with reverse-blend
        // protection on the sampler side. Yaw still responds (the
        // turn-toward-intent below isn't gated), so the player can
        // re-aim mid-recovery; just no walking commit.
        const bool wasd_intent_raw = glm::length(moveIntent) > 0.0001f;
        // Debounce: SM commits to a wasd_intent change only after the
        // raw value has disagreed continuously with the stable value
        // for wasd_debounce_seconds. Held WASD reaches walking after
        // the debounce delay; a brief tap (less than debounce) never
        // commits; rapid mashing never accumulates because each
        // tap-release resets the disagreement timer.
        if (wasd_intent_raw == sStableWasdIntent)
        {
            sWasdDisagreeStartedAt = -1.0f; // raw matches stable; no pending change
        }
        else if (sWasdDisagreeStartedAt < 0.0f)
        {
            sWasdDisagreeStartedAt = selva::wallClock(); // first frame of disagreement
        }
        else if (selva::wallClock() - sWasdDisagreeStartedAt >= tun.wasd_debounce_seconds)
        {
            sStableWasdIntent = wasd_intent_raw;
            sWasdDisagreeStartedAt = -1.0f;
        }
        const bool wasd_intent = sStableWasdIntent;
        const bool attack_in_flight = sSampler.isOneShotActive() || sPendingFirstAction.active;
        const bool in_attack_recovery = selva::wallClock() < selva::combat::locoLockoutUntil();
        const bool loco_lockout = attack_in_flight || in_attack_recovery;
        // WASD override: if the player is pushing a direction, keep the
        // loco track on a gait clip regardless of lockout. The Full-
        // body attack one-shot drives the legs during the swing; the
        // gait clip plays underneath so the attack ends with running
        // or walking already on the loco track — no combat-idle pop
        // when the BlendOut reveals it. Without this, releasing sprint
        // mid-attack drops loco to combat-idle (the SM's Idle target),
        // and the BlendOut reveals combat-idle for ~0.2s before the
        // lockout expires and SM swaps to walking.
        const bool is_moving = wasd_intent;
        const bool is_sprinting = sPlayer.sprinting && wasd_intent;
        // Trace every change in (wasd_intent, attack_in_flight,
        // in_attack_recovery, is_moving). Lets us correlate input
        // edges to SM decisions to spasm timing.
        if (selva::combat::isCombatDebugEnabled())
        {
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
        const bool clip_done_this_frame = sSampler.locomotionClipFinished();
        // "Armed" = at least one hand has a weapon equipped. The
        // combat-ready idle clip differs by armed state — sword-and-
        // shield guard pose vs unarmed brawler bounce.
        // "Armed" means holding a real weapon — fists (the synthesized
        // unarmed default) don't count, so the combat-idle picker
        // chooses unarmed_combat_idle, not sword_and_shield_idle_4.
        const bool is_armed = !isUnarmed(sEquipment);
        const auto sm_out = tickLocomotionStateMachine(
            sLocomotionSM, is_moving, is_sprinting, clip_done_this_frame,
            combat_input_this_frame, dt, tun.combat_idle_grace_seconds, is_armed,
            selva::wallClock());
        clip_name = sm_out.clip_name;
        clip_loops = sm_out.loops;
        clip_blend_seconds = sm_out.blend_seconds;
        // Held-block override: when blocking, force the locomotion
        // track to play the block_idle loop. The block_1 raise
        // animation is firing as a one-shot on the upper-body track
        // (full mask actually), so overriding the loco track here
        // doesn't conflict during the raise — the one-shot dominates.
        // After the raise finishes, the loco track's block_idle
        // becomes visible and persists the held-shield pose. On
        // release, sBlockingActive flips false, the override drops,
        // and the SM picks combat-idle / walking / running normally.
        // Held-block locomotion override only applies when there's a
        // dedicated block-idle clip (buckler does, unarmed doesn't —
        // unarmed holds the last frame of the one-shot itself).
        if (sBlockingActive && offHandCanBlock(sEquipment))
            clip_name = "sword_and_shield_block_idle";

        // Loco freeze during one-shot BlendIn. While the one-shot is
        // ramping in (phase 1), the loco track and one-shot are both
        // partially visible; letting the loco track ALSO swap clips
        // at that moment creates a multi-source collision (one-shot
        // ramp + loco crossfade + new loco clip splice) that reads
        // as a leg spasm. During Hold (phase 2), the one-shot is
        // fully on (weight=1) so the loco track is invisible — it
        // can swap freely underneath. By the time BlendOut (phase 3)
        // begins, the loco track has had time to settle on its new
        // clip in isolation, so the reveal is clean.
        const auto fd_loco = sSampler.frameDiagnostics();
        const bool one_shot_blending_in = fd_loco.one_shot_phase == 1;
        if (one_shot_blending_in && !sLastLocoClipName.empty() &&
            clip_name != sLastLocoClipName)
        {
            clip_name = sLastLocoClipName;
        }
        clip = sClips.get(clip_name);
        const bool loco_clip_changed = (clip_name != sLastLocoClipName);
        // Trace SM clip-choice changes. Pairs with the input trace so
        // we can see exactly which press caused which transition.
        // Includes loco_current_time so we know what frame of the
        // outgoing clip is the splice source — combined with the
        // dest clip's t=0, that's enough to compute pose mismatch.
        // Capture pre-update live joint positions for the post-update
        // residual log (the actual splice point the sampler picked is
        // only known AFTER update runs).
        if (selva::combat::isCombatDebugEnabled() && loco_clip_changed)
        {
            const auto fd = sSampler.frameDiagnostics();
            combatLog("[sm %.4fs] loco-pick %s@%.3fs -> %s (blend=%.3fs)\n", selva::wallClock(),
                      sLastLocoClipName.c_str(), fd.loco_current_time, clip_name.c_str(),
                      sm_out.blend_seconds);
            const char* names[] = {"mixamorig:RightHand", "mixamorig:LeftHand",
                                   "mixamorig:RightFoot", "mixamorig:LeftFoot",
                                   "mixamorig:Hips"};
            for (const char* n : names)
            {
                const int idx = sSampler.findJoint(n);
                if (idx < 0)
                    continue;
                pre_update_joints.push_back({n, idx, sSampler.jointWorldPos(idx)});
            }
        }
        // Stash for next-frame combat-fire diagnostics (input runs
        // before this block, so we expose the previous frame's pick).
        sLastLocoClipName = clip_name;
        // Per-clip override from locomotion.json (looped clips like
        // walking/running may want longer/shorter blends than the SM
        // default). Transition clips fall back to the SM-supplied
        // 0.10s when no override exists.
        clip_blend_seconds =
            sLocomotionConfig.blendInSeconds(clip_name, clip_blend_seconds);
    }
    if (clip != nullptr && clip->isLoaded())
    {
        ZoneScopedN("sampler.update");
        sSampler.update(*clip, dt, clip_blend_seconds, clip_loops);
    }

    // Post-update residual diagnostic: now that the sampler has chosen
    // the splice time (t=0, phase-matched, cached, or pose-matched),
    // sample the new clip at THAT time and log the per-joint residual
    // vs the pre-update live pose. This is the actual offset the
    // inertialization decay has to absorb.
    if (!pre_update_joints.empty() && clip != nullptr)
    {
        const auto fd = sSampler.frameDiagnostics();
        const float splice_t = fd.loco_current_time;
        for (const auto& j : pre_update_joints)
        {
            const glm::vec3 cand = sSampler.sampleJointWorldPos(*clip, splice_t, j.idx);
            combatLog("  %s residual=|%.3fm|  live=(%.2f,%.2f,%.2f) splice_t=%.3fs cand=(%.2f,%.2f,%.2f)\n",
                      j.name, glm::length(cand - j.live), j.live.x, j.live.y, j.live.z, splice_t,
                      cand.x, cand.y, cand.z);
        }
    }

    // Sticky per-frame loco-state trace. Whenever the loco_blend_weight
    // is mid-ramp (not 0 nor 1) OR a one_shot is blending, dump the
    // sampler's state every frame. Lets us watch the rapid-WASD spasm
    // unfold tick-by-tick.
    if (selva::combat::isCombatDebugEnabled())
    {
        const auto fd = sSampler.frameDiagnostics();
        const bool loco_mid = fd.loco_blend_weight > 0.001f && fd.loco_blend_weight < 0.999f;
        const bool one_shot_mid = fd.one_shot_weight > 0.001f && fd.one_shot_weight < 0.999f;
        if (loco_mid || one_shot_mid)
        {
            combatLog("[fr %.4fs] loco_w=%.3f one_shot_w=%.3f phase=%d clip=%s\n",
                      selva::wallClock(), fd.loco_blend_weight, fd.one_shot_weight,
                      fd.one_shot_phase, sLastLocoClipName.c_str());
            // Joint positions during the one-shot fade-out (phase=3)
            // capture the visible spasm region — this is when the
            // one-shot's pose hands off to the loco track. If hip
            // y plummets here, the discontinuity is on this handoff.
            if (fd.one_shot_phase == 3)
            {
                for (const char* n : {"mixamorig:Hips", "mixamorig:LeftFoot",
                                      "mixamorig:RightFoot"})
                {
                    const int idx = sSampler.findJoint(n);
                    if (idx < 0)
                        continue;
                    const glm::vec3 p = sSampler.jointWorldPos(idx);
                    combatLog("    [fade] %s = (%.2f,%.2f,%.2f)\n", n, p.x, p.y, p.z);
                }
            }
        }
    }

    // Root-motion translation (locomotion). When enabled and not mid-
    // dodge, world translation comes from the active clip's authored
    // hip XZ motion. consumedHipDelta gives the per-frame motion in
    // clip-local model space (XZ; Y is zeroed by the freeze). We rotate
    // it through the renderer's model rotation (yaw + π around Y) to
    // land it in world frame.
    //
    // Rotation derivation: glm::rotate(M, θ, Y) applied to (dx, 0, dz)
    // gives (cos(θ)·dx + sin(θ)·dz, 0, -sin(θ)·dx + cos(θ)·dz). With
    // θ = yaw + π (cos = -cos(yaw), sin = -sin(yaw)) this becomes:
    //   world.x = -cos(yaw) · dx - sin(yaw) · dz
    //   world.z =  sin(yaw) · dx - cos(yaw) · dz
    //
    // Sanity check: clip-local +Z (Mixamo bind forward) at yaw=0 →
    // world (0, 0, -1) = north. Player facing east (yaw=-π/2) → world
    // (1, 0, 0) = east. Both match the player's visual forward
    // (facing_world = (-sin(yaw), 0, -cos(yaw))).
    //
    // Skipping during sDodgeActive — the dodge translation block below
    // owns world push during a dodge so we don't double-translate
    // (locomotion track is still advancing underneath the one-shot).
    if (!sDodgeActive && sDebugClipName.empty())
    {
        const glm::vec3 hip_local = sSampler.consumedHipDelta();
        if (glm::length(glm::vec2(hip_local.x, hip_local.z)) > 1e-6f)
        {
            const float sy = std::sin(sPlayer.yaw);
            const float cy = std::cos(sPlayer.yaw);
            const glm::vec3 hip_world(
                -cy * hip_local.x - sy * hip_local.z, 0.0f,
                 sy * hip_local.x - cy * hip_local.z);
            sPlayer.pos += hip_world;
        }
    }

    // Dodge translation. The clip's authored hip motion drives world
    // translation directly: roll clip moves the hip in clip-local +Z
    // (forward); rotated by sPlayer.yaw it lands as world-frame
    // forward. Backstep clip moves the hip in clip-local -Z; same
    // rotation yields backward. No script-distance / direction-sign
    // logic needed — the clip data carries both magnitude and direction.
    //
    // Mid-roll steering: rolls let you bend the trajectory by moving
    // WASD/the camera during the clip; the per-frame yaw change re-
    // rotates the next frame's hip delta into the new direction.
    // Backsteps don't steer — the defensive beat commits to its
    // initial axis.
    if (sDodgeActive && sDebugClipName.empty())
    {
        if (!sDodgeIsBackstep && glm::length(moveIntent) > 0.0001f)
        {
            const glm::vec3 dir = glm::normalize(moveIntent);
            const float targetYaw = yawFromGroundDir(dir);
            float delta = wrapAngleSigned(targetYaw - sPlayer.yaw);
            const float maxStep = tun.dodge_steer_rate * dt;
            if (delta > maxStep)
                delta = maxStep;
            else if (delta < -maxStep)
                delta = -maxStep;
            sPlayer.yaw += delta;
        }

        const glm::vec3 hip_local = sSampler.consumedHipDelta();
        if (glm::length(glm::vec2(hip_local.x, hip_local.z)) > 1e-6f)
        {
            const float sy = std::sin(sPlayer.yaw);
            const float cy = std::cos(sPlayer.yaw);
            const glm::vec3 hip_world(
                -cy * hip_local.x - sy * hip_local.z, 0.0f,
                 sy * hip_local.x - cy * hip_local.z);
            sPlayer.pos += hip_world;
        }
    }

    // CSV recording — capture every frame's joint world-space positions
    // while a recording is in progress. Stops automatically when the
    // requested duration elapses, then writes out a CSV file.
    if (sDebugRecording)
    {
        DebugBoneSample sample;
        sample.time_s = sDebugRecordElapsed;
        const int n = sSampler.jointCount();
        sample.joints.reserve(n);
        for (int i = 0; i < n; ++i)
            sample.joints.push_back(sSampler.jointWorldPos(i));
        sDebugRecordSamples.push_back(std::move(sample));
        sDebugRecordElapsed += dt;
        if (sDebugRecordElapsed >= sDebugRecordDuration)
        {
            // Flush to CSV. Path is relative to working directory
            // (typically build/bin); the file ends up alongside the
            // game exe so it's easy to find.
            const std::string out_path = "debug_bones_game_" + sDebugRecordClipName + ".csv";
            std::FILE* fp = std::fopen(out_path.c_str(), "w");
            if (fp != nullptr)
            {
                std::fprintf(fp, "time_s,joint_name,x,y,z\n");
                const int nj = sSampler.jointCount();
                for (const auto& s : sDebugRecordSamples)
                {
                    for (int i = 0; i < nj && i < static_cast<int>(s.joints.size()); ++i)
                    {
                        std::fprintf(fp, "%.4f,%s,%.6f,%.6f,%.6f\n", s.time_s,
                                     sSampler.jointName(i), s.joints[i].x, s.joints[i].y,
                                     s.joints[i].z);
                    }
                }
                std::fclose(fp);
                std::fprintf(stderr, "[anim-debug] wrote %s (%zu frames)\n", out_path.c_str(),
                             sDebugRecordSamples.size());
            }
            else
            {
                std::fprintf(stderr, "[anim-debug] failed to open %s for write\n",
                             out_path.c_str());
            }
            sDebugRecording = false;
            sDebugRecordSamples.clear();
        }
    }
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
bool& showTuningPanel() { return ::sShowTuningPanel; }
std::string& debugClipName() { return ::sDebugClipName; }
bool& debugLoop() { return ::sDebugLoop; }
bool& debugRecording() { return ::sDebugRecording; }
float& debugRecordElapsed() { return ::sDebugRecordElapsed; }
float& debugRecordDuration() { return ::sDebugRecordDuration; }
std::string& debugRecordClipName() { return ::sDebugRecordClipName; }
bool& debugRecordArmedNextDodge() { return ::sDebugRecordArmedNextDodge; }
bool& frameCaptureArmedNextDodge() { return ::sFrameCaptureArmedNextDodge; }
bool& frameCaptureArmedNextChain() { return ::sFrameCaptureArmedNextChain; }
bool frameCaptureActive() { return ::sFrameCaptureActive; }
float frameCaptureElapsed() { return ::sFrameCaptureElapsed; }
float frameCaptureDuration() { return ::sFrameCaptureDuration; }
int frameCaptureCounter() { return ::sFrameCaptureCounter; }
std::size_t debugRecordSampleCount() { return ::sDebugRecordSamples.size(); }
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
