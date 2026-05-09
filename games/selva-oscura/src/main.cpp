// stb_image_write — single-header PNG writer for the F1-debug frame
// capture feature. STB_IMAGE_WRITE_IMPLEMENTATION must live in exactly
// one .cpp file across the binary; main.cpp is the canonical place.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Engine.h"
#include "Tunables.h"
#include "WallClock.h"
#include "combat/CombatLog.h"
#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/LocomotionConfig.h"
#include "anim/PoseSampler.h"
#include "anim/SkeletalMesh.h"
#include "anim/SkeletalRenderer.h"
#include "anim/Skeleton.h"
#include "combat/PlayerEquipment.h"
#include "combat/Weapon.h"
#include "combat/WeaponClass.h"
#include "render/SceneGeometry.h"
#include "render/SceneShaders.h"

#include <imgui.h>
// stb_image (reader) is for the contact-sheet composite; its
// IMPLEMENTATION already lives in engine/src/TextureManager.cpp, so we
// just need the declarations here.
#include <stb_image.h>
#include <stb_image_write.h>
#include <tracy/Tracy.hpp>

// Tell SDL not to redefine `main` to its WinMain shim — the engine owns SDL
// init, this file just uses input/state APIs. Must be before <SDL.h>.
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

// Scene shaders + scene geometry (cube/floor/grid/axes) live in
// render/SceneShaders.{h,cpp} and render/SceneGeometry.{h,cpp}.


// ---------------------------------------------------------------------------
// Skeletal character assets — X_Bot.glb (mesh + skeleton) + the entire
// Pro Sword and Shield Pack (locomotion + attack clips). All assets share
// the canonical Mixamo skeleton, so no retargeting is needed at runtime.
// Loaded once at startup via initSkeletalAssets(); torn down via
// shutdownSkeletalAssets() before the GL context dies. The pose sampler
// caches per-clip interpolation state across frames; reusing it (vs
// constructing a new one each frame) is what makes sampling allocation-
// free in steady state.
// ---------------------------------------------------------------------------

static selva::anim::Skeleton sSkeleton;
static selva::anim::SkeletalMesh sPlayerMesh;
static selva::anim::ClipRegistry sClips;
static selva::anim::PoseSampler sSampler;
static selva::anim::LocomotionConfig sLocomotionConfig;

// Convenience accessors for the locomotion clips. Clip names are
// sanitized at conversion time (lowercase, spaces → underscores). These
// are the *generic* (no-weapon) base locomotion clips. Per-weapon stance
// overlays (sword grip pose, shield grip pose) layer on top via the
// upper-body mask system — that's a follow-up; for now the bare
// locomotion plays regardless of equipped weapon.
//
// Clip choices come from the Phase-0 audit (clipHipPathLength) — we
// need clips with authored hip motion so the world push can be derived
// from clip data instead of a script speed:
//   * standard_idle  — 3.00s, 0.008m hip path. True stationary; no
//                      idle drift. Generic-pack idles ship with In Place
//                      ON which is exactly what we want for idle.
//   * walking        — 1.03s, 1.85m hip path → ~1.79 m/s authored speed.
//                      From Action Adventure pack; In Place was off.
//   * running        — 0.70s, 3.32m hip path → ~4.75 m/s authored speed.
//                      From Action Adventure pack; In Place was off.
// The standard_walk / standard_run pair is unusable as root motion
// (0.22m hip path each — pure in-place sway). Kept loaded for backward
// compatibility / debug-clip preview but not selected by gameplay.
static const selva::anim::AnimationClip* idleClip()
{
    return sClips.get("standard_idle");
}
static const selva::anim::AnimationClip* walkClip()
{
    return sClips.get("walking");
}
static const selva::anim::AnimationClip* runClip()
{
    return sClips.get("running");
}

// Animation time bookkeeping now lives inside the PoseSampler, which
// manages its own per-clip clocks and a cross-fade between two
// concurrent tracks. main.cpp just calls sSampler.update(clip, dt, fade).

static bool initSkeletalAssets()
{
    sSkeleton = selva::anim::loadSkeleton("assets/characters/x_bot/skeleton.ozz");
    if (!sSkeleton.isLoaded())
        return false;
    // Bulk-load every .ozz in the x_bot asset dir at once. This pulls in
    // every clip the build produced from the Mixamo pack — locomotion
    // (sword_and_shield_idle/walk/run) plus all the attack clips. Anything
    // missing just won't be in the registry; gameplay code falls back
    // gracefully when the lookup returns null.
    const int n_clips = sClips.loadDirectory("assets/characters/x_bot");
    std::fprintf(stderr, "[anim] loaded %d clip(s) from assets/characters/x_bot\n", n_clips);
    if (idleClip() == nullptr || !idleClip()->isLoaded())
    {
        std::fprintf(stderr, "[anim] required idle clip missing — character disabled\n");
        return false;
    }
    sPlayerMesh = selva::anim::loadSkeletalMesh("assets/characters/x_bot/X_Bot.glb", sSkeleton);
    if (!sPlayerMesh.isLoaded())
        return false;
    // The sampler is bound to the (skeleton, mesh) pair at construction so
    // its bone-palette space is guaranteed to match the mesh's baked
    // vertex space. No "remember to wire the root transform" step.
    sSampler = selva::anim::createPoseSampler(sSkeleton, sPlayerMesh);
    if (!selva::anim::initSkeletalRenderer())
        return false;

    // Pre-warm: sample the first clip at t=0 so the bone palette is
    // populated before the first frame renders. Without this, the very
    // first render uses an uninitialized palette (zeros), producing a
    // flash of broken geometry. Partial fix only; see
    // docs/BACKLOG.md "First-frame pop / init flash" for the full
    // story (camera + player prev-state lerps still pop on frame 0).
    // dt=0 advances no time; blend=0 snaps without fading. Pre-warm fills
    // the bone palette with the Idle pose at frame 0 so the first render
    // doesn't see zero matrices.
    sSampler.update(*idleClip(), 0.0f, 0.0f);
    // Configure per-joint inertialization decay scaling. Each joint's
    // decay window scales with its pose-offset magnitude; small-offset
    // joints stay snappy, large-offset joints get longer windows so
    // their motion reads as smooth rather than as a fast snap.
    {
        const auto& tun = selva::tuning::current();
        sSampler.setInertializationScaling(tun.inertialize_decay_base_seconds,
                                           tun.inertialize_decay_scale_per_radian,
                                           tun.inertialize_decay_max_seconds);
    }
    return true;
}

// Audit: scan every loaded clip's hip-XZ path length so we can see at
// a glance which clips ship with authored hip motion (suitable for
// driving world translation as root motion) and which are "in place"
// (need to be ignored or re-downloaded).
//
//   * 0.00m  — In Place toggle was on; clip won't drive world travel.
//   * <0.05m — micro hip sway only (idles); world-drift if not flagged.
//   * >0.5m  — clip authors meaningful translation; usable as root motion.
//
// Read once; the values inform the locomotion.json is_stationary flag
// and tell us whether walk/run/dodge clips need re-downloading from
// Mixamo before the root-motion refactor proceeds.
static void auditClipHipMotion()
{
    if (sClips.by_name.empty() || sSampler.bone_palette.empty())
        return;
    // Gather then sort by name for stable output ordering.
    std::vector<std::string> names;
    names.reserve(sClips.by_name.size());
    for (const auto& kv : sClips.by_name)
        names.push_back(kv.first);
    std::sort(names.begin(), names.end());

    std::fprintf(stderr,
                 "[clip-audit] hip XZ path length per clip (full clip, not motion-end-clipped):\n");
    std::fprintf(stderr, "  %-40s %8s %8s\n", "clip", "duration", "hip_path");
    for (const auto& name : names)
    {
        const auto* clip = sClips.get(name);
        if (clip == nullptr || !clip->isLoaded())
            continue;
        const auto scan = sSampler.clipHipPathLength(*clip, 60.0f, 0.0f);
        std::fprintf(stderr, "  %-40s %7.2fs %7.3fm\n", name.c_str(), clip->duration(),
                     scan.path_length);
    }

    // For dodge clips specifically, dump the hip-XZ trajectory at 11
    // evenly-spaced timesteps. This shows where the clip authors most
    // of its travel — front-loaded vs back-loaded vs evenly distributed.
    // If skip-in (roll_skip_in_seconds) eats the windup window where
    // most travel happens, root motion delivers almost no world push
    // even though the clip visually rolls. The data tells us whether
    // skip-in is the culprit.
    static const char* kProfileClips[] = {
        // Dodge / roll candidates
        "stand_to_roll",
        "standing_dodge_backward",
        "falling_to_roll",
        // Sword & shield attacks (referenced by config/weapon_classes/sword.json).
        // We profile these to see whether the clip authors net forward
        // hip travel (clean root-motion source) or ping-pongs back to
        // origin (visually rolls/swings but the character ends where
        // they started). The data informs whether attacks need a script-
        // push fallback like the legacy roll did, or can ride pure root
        // motion like falling_to_roll does.
        "sword_and_shield_slash",
        "sword_and_shield_slash_2",
        "sword_and_shield_slash_3",
        "sword_and_shield_slash_4",
        "sword_and_shield_slash_5",
        "sword_and_shield_attack",
        "sword_and_shield_attack_2",
        "sword_and_shield_attack_3",
        "sword_and_shield_attack_4",
    };
    for (const char* nm : kProfileClips)
    {
        const auto* clip = sClips.get(nm);
        if (clip == nullptr || !clip->isLoaded())
            continue;
        const float dur = clip->duration();
        std::fprintf(stderr, "[clip-profile] %s  dur=%.2fs  hip XZ trajectory:\n", nm, dur);
        std::fprintf(stderr,
                     "  %4s %5s %8s %8s %10s %12s\n",
                     "t%", "t(s)", "hip_x", "hip_z", "step", "cumul_dist");
        glm::vec2 prev(0.0f);
        float cumul = 0.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float t = (i / 10.0f) * dur;
            const glm::vec2 hip = sSampler.sampleHipXZAt(*clip, t);
            const float step = (i == 0) ? 0.0f : glm::length(hip - prev);
            cumul += step;
            std::fprintf(stderr, "  %3d%% %5.2f %8.3f %8.3f %10.3f %12.3f\n",
                         i * 10, t, hip.x, hip.y, step, cumul);
            prev = hip;
        }
    }
}

static void shutdownSkeletalAssets()
{
    selva::anim::shutdownSkeletalRenderer();
    // sPlayerMesh's destructor frees its GPU buffers. The other ozz-owned
    // structs free heap memory in their destructors — no GL involvement.
}

// ---------------------------------------------------------------------------
// Resource cleanup
// ---------------------------------------------------------------------------

static void shutdownGeometry()
{
    selva::render::shutdownSceneGeometry();
    selva::render::shutdownSceneProgram();
}

// ---------------------------------------------------------------------------
// Player + camera state
// ---------------------------------------------------------------------------

// Numeric "feel" parameters live in selva::tuning::Tunables (Tunables.h),
// loaded from config/tunables.json at startup, edited at runtime via the
// F1 ImGui panel. Gameplay code and ProceduralDriver both read from the
// same global — see selva::tuning::current().

struct PlayerState
{
    // Y is unused for movement — gameplay is XZ-only on the floor plane,
    // and the renderer plants the character's feet via -foot_offset_y.
    // Keep at zero for clarity (a non-zero Y would be silently ignored).
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
    float yaw = 0.0f;       // facing yaw in radians; 0 = facing -Z
    bool sprinting = false; // true while Space has been held past the sprint commit threshold
};

// One global player. When this scales (multiple controllable entities, NPCs
// using the same locomotion code), promote to ECS.
static PlayerState sPlayer;

// Pick the animation clip that should drive the character this frame, based
// on the player's gameplay state. Falls back to Idle if a more specific
// clip isn't loaded (graceful degradation on a fresh checkout where only
// some animations have been processed). Returns null only if even Idle
// isn't loaded — the caller should skip sampling in that case.
//
// This is the seam between gameplay and animation. Combat will extend it
// with attack / parry / hit-react states; each new state maps to a clip
// pointer here, and the AnimationDriver's procedural offsets compose on
// top. Keeping the selection in one named function makes the mapping
// inspectable and cheap to grep when debugging "why is this state
// playing the wrong clip."
// Locomotion state machine — discrete-state. States are (Idle,
// Walk, Run, plus "in-transition"). Transitions between states are
// driven by player intent (is_moving, is_sprinting). Some transitions
// have an explicit transition clip authored to look right (Idle→Walk
// plays start_walking; Run→Idle plays run_to_stop). Other transitions
// fall through to a phase-matched crossfade between loop clips
// (Walk↔Run share gait structure, the crossfade reads cleanly).
//
// State machine yields a clip name + loops? + blend per frame; gameplay
// feeds those into PoseSampler::update(). Transition clips are
// non-looping; when the clip finishes (locomotionClipFinished() true),
// the SM enters its `target` state and starts the destination loop.
//
// The SM is the single source of truth for "what locomotion clip
// should play right now" — replacing the prior selectClipNameForPlayer
// which only knew about loops.
enum class LocomotionState
{
    Idle,
    Walk,
    Run,
    Transitioning, // playing a transition clip; target holds the destination
};

// Combat-readiness — orthogonal to LocomotionState. Idle/Walk/Run answer
// "how is the body moving"; CombatStance answers "is the body braced for
// a fight." The two axes compose: Idle+Peaceful uses standard_idle;
// Idle+CombatReady uses sword_and_shield_idle_4 (a tight, weight-forward
// guard pose).
//
// Why a parallel axis instead of more LocomotionStates? Because every
// movement state can occur in either combat or peaceful flavor — a
// peaceful walk and a combat walk are distinct clips. Splitting along
// the second axis keeps the enum small (3 movement states × 2 stances =
// 6 logical poses, expressed as 3+2 not 6).
//
// Entry: any combat input (LMB/RMB/heavy/block) flips the player into
// CombatReady. Exit: after combat_idle_grace_seconds of no combat input
// (and, eventually, no nearby threats). Transitioning between the two
// stances is a clip blend; iteration will tell us whether we need
// authored "sheath" / "draw" beats.
enum class CombatStance
{
    Peaceful,
    CombatReady,
};

// Which foot leads in CombatReady. Default = the authored Mixamo stance
// (right hand back, weapon ready). Mirror = future opposite-foot moveset
// variant. Slot is wired now (Shift+F flips it) so a second moveset can
// hook in without restructuring; currently visual selection is identical
// (clip table doesn't yet branch on foot). When a mirror moveset lands,
// chooseLocomotionClip / resolveAttackChainEntry will pick by foot.
enum class CombatStanceFoot
{
    Default,
    Mirror,
};

struct LocomotionStateMachine
{
    LocomotionState current = LocomotionState::Idle;
    LocomotionState target = LocomotionState::Idle;
    // Set when current==Transitioning. Identifies which transition
    // clip to play. Cleared when the transition completes.
    const char* active_transition_clip = nullptr;
    bool active_transition_loops = false; // false for actual transition clips

    CombatStance combat_stance = CombatStance::Peaceful;
    CombatStanceFoot stance_foot = CombatStanceFoot::Default;
    // Wall-clock seconds until which CombatReady stance is locked
    // active. Each combat input pushes this forward by at least
    // combat_idle_grace_seconds. Attack-fire callers push it forward
    // by clip_duration + grace so the stance holds until the
    // attack's recovery has ended (not just until the press fires).
    // Without this anchoring, the finisher's last-clip seconds elapse
    // before the timer notices, and the stance drops to peaceful while
    // the player is still mid-recovery — visually jarring "I just
    // attacked, why am I back to casual idle?"
    float stance_active_until = 0.0f;

    // Decide the destination state from current player intent.
    static LocomotionState desiredFromIntent(bool is_moving, bool is_sprinting)
    {
        if (!is_moving)
            return LocomotionState::Idle;
        if (is_sprinting)
            return LocomotionState::Run;
        return LocomotionState::Walk;
    }
};

// What the SM tells the sampler to play this frame.
struct LocomotionFrameOutput
{
    const char* clip_name = "standard_idle";
    bool loops = true;
    float blend_seconds = 0.20f;
};

// Tick the locomotion state machine. Pure logic — looks at intent and
// the sampler's locomotionClipFinished() to decide if the active
// transition should hand off to the destination state.
//
// State graph today:
//   All transitions: phase-matched crossfade between loop clips
//   (handled by PoseSampler internally; phase-match only kicks in
//   between gait-cycle clips like walk↔run, otherwise plain blend).
//
// Earlier we wired explicit transition clips (start_walking,
// run_to_stop) but Mixamo's `start_walking` is effectively another
// (slower) walk loop rather than a true transition beat — playing it
// as Idle→Walk meant ~3s of slow walking before the regular gait
// kicked in. Without a curated transition library it's cleaner to
// crossfade. Re-add transitions per-edge as we acquire properly
// authored clips.
//
// Adding a transition later: extend selectTransitionClip() to return
// a clip name + loops=false for the (from, to) pair you want to
// override. Returning nullptr means "use a crossfade."
static const char* selectTransitionClip(LocomotionState /*from*/, LocomotionState /*to*/,
                                        bool* out_loops)
{
    *out_loops = false;
    return nullptr;
}

static const char* loopClipForState(LocomotionState s, CombatStance stance, bool is_armed)
{
    // Combat-ready idle splits by armed-ness:
    //   * Armed — sword_and_shield_idle_4 (2.50s, ~2cm hip drift, tight
    //     guard pose, weight forward — reads as "I'm holding a weapon
    //     ready to swing").
    //   * Unarmed — unarmed_combat_idle (a bouncier weight-shifting
    //     fight stance — reads as "I'm ready to brawl"). The armed
    //     idle would look wrong on a bare-handed character.
    // Peaceful idle is the casual standing standard_idle in either
    // case. Walk/run currently share the same peaceful clip across
    // stances — combat-aware walk/run variants are a future polish.
    switch (s)
    {
    case LocomotionState::Idle:
        if (stance == CombatStance::CombatReady)
            return is_armed ? "sword_and_shield_idle_4" : "unarmed_combat_idle";
        return "standard_idle";
    case LocomotionState::Walk:
        return "walking";
    case LocomotionState::Run:
        return "running";
    case LocomotionState::Transitioning:
        // Caller should be using sm.active_transition_clip instead.
        return "standard_idle";
    }
    return "standard_idle";
}

static LocomotionFrameOutput tickLocomotionStateMachine(LocomotionStateMachine& sm,
                                                       bool is_moving, bool is_sprinting,
                                                       bool clip_finished_this_frame,
                                                       bool combat_input_this_frame, float dt,
                                                       float combat_grace_seconds, bool is_armed,
                                                       float wall_clock_seconds)
{
    // Combat-stance update first — orthogonal axis to movement.
    // Any combat input pushes stance_active_until forward by at
    // least the grace window. Attack-fire callers push it further
    // (by clip_duration + grace) so the stance holds through the
    // attack's recovery, not just from the press moment.
    if (combat_input_this_frame)
    {
        sm.combat_stance = CombatStance::CombatReady;
        const float new_until = wall_clock_seconds + combat_grace_seconds;
        if (new_until > sm.stance_active_until)
            sm.stance_active_until = new_until;
    }
    if (sm.combat_stance == CombatStance::CombatReady &&
        wall_clock_seconds >= sm.stance_active_until)
    {
        sm.combat_stance = CombatStance::Peaceful;
    }

    LocomotionFrameOutput out;
    const LocomotionState desired =
        LocomotionStateMachine::desiredFromIntent(is_moving, is_sprinting);

    // 1. If we're mid-transition: hand off to target when the clip ends.
    //    If the player's intent changes mid-transition we let the
    //    current transition finish first (no ducking out
    //    of the start_walking beat halfway). Future polish can cancel
    //    interruptible transitions.
    if (sm.current == LocomotionState::Transitioning)
    {
        if (clip_finished_this_frame)
        {
            sm.current = sm.target;
            sm.active_transition_clip = nullptr;
            // Fall through to evaluate the new state immediately so we
            // don't wait an extra frame to start the target loop.
        }
        else
        {
            out.clip_name = sm.active_transition_clip
                                ? sm.active_transition_clip
                                : loopClipForState(sm.target, sm.combat_stance, is_armed);
            out.loops = sm.active_transition_loops;
            out.blend_seconds = 0.10f;
            return out;
        }
    }

    // 2. Steady state: in current, intent matches.
    if (desired == sm.current)
    {
        out.clip_name = loopClipForState(sm.current, sm.combat_stance, is_armed);
        out.loops = true;
        return out;
    }

    // 3. State change required: try a transition clip, else crossfade.
    bool tloops = false;
    const char* transition = selectTransitionClip(sm.current, desired, &tloops);
    if (transition != nullptr)
    {
        sm.active_transition_clip = transition;
        sm.active_transition_loops = tloops;
        sm.target = desired;
        sm.current = LocomotionState::Transitioning;
        out.clip_name = transition;
        out.loops = tloops;
        out.blend_seconds = 0.10f;
        return out;
    }
    // No transition clip — switch state immediately and let the
    // sampler's phase-matched crossfade handle the blend.
    sm.current = desired;
    out.clip_name = loopClipForState(desired, sm.combat_stance, is_armed);
    out.loops = true;
    out.blend_seconds = 0.20f;
    return out;
}

static LocomotionStateMachine sLocomotionSM;

// Camera orientation. Yaw rotates around world-up (Y), pitch tilts up/down.
// Yaw=0 looks down -Z; positive yaw rotates CCW looking down (right-handed).
// Pitch is clamped to avoid gimbal flip at the poles.
static float sCamYaw = 0.0f;
static float sCamPitch = -0.25f; // start slightly looking down

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

// Which attack slot the input combination maps onto.
//   Light   — bare LMB/RMB while standing or walking.
//   Heavy   — Shift+LMB/RMB.
//   Running — sprint attack: LMB/RMB while sprinting. Falls
//             back to Light if the weapon class doesn't define a running
//             clip — bucklers, for instance, leave the slot empty.
enum class AttackKind
{
    Light,
    Heavy,
    Running,
};

// Combo / chain state, per hand. Each hand has its own chain because
// LMB and RMB resolve to different weapons and chains run independently
// — you can be mid-chain on the right hand while pressing the left
// hand's first attack.
//
// `chain_index` is the next entry to play on the next press. After the
// chain's last attack, it stays past-end; `combo_reset_grace_seconds`
// of inactivity resets it to 0. Switching attack kind (light → heavy,
// or running → light, etc.) also resets the chain — each
// kind is its own chain.
//
// `chain_kind` records the AttackKind the chain belongs to so we can
// detect kind-switches and reset.
//
// `cancel_window_open_at` is the wall-clock time when the current
// attack's cancel window starts. Before that time, presses are
// buffered (see sBufferedPress*) but don't fire. At/after that time,
// a press immediately steps the chain.
//
// `chain_reset_at` is the wall-clock time at which the chain auto-
// resets to 0 if no further press lands. Set to "wall_clock_now +
// current_attack_duration + combo_reset_grace_seconds" each time a
// chain step fires.
struct AttackChainState
{
    int chain_index = 0;
    // Index into the technique list for this kind/grip. Locked at
    // first press if multiple techniques share slot-0 button; locked
    // at second press otherwise. -1 = not yet locked.
    int technique_index = -1;
    AttackKind chain_kind = AttackKind::Light;
    // Bounded rhythm window. Press inside = hit, before/after = miss.
    float cancel_window_open_at = 0.0f;
    float cancel_window_close_at = 0.0f;
    float chain_reset_at = 0.0f;
    // Last press accuracy: 1.0 = window center, 0.0 = window edges.
    float last_press_accuracy = 0.0f;
    bool last_press_was_perfect = false;
    // True when the most-recently-fired attack was the final entry
    // of its chain. While true, cancellation is disabled — the
    // attack must play out fully and chain_reset_at must elapse
    // before the next press is valid (and starts a fresh chain at
    // index 0). Last-hit-commits rule.
    bool is_finisher = false;
};
static AttackChainState sChainRight;
static AttackChainState sChainLeft;

// Per-hand input buffer: a too-early press during another attack's
// non-cancellable window stays valid for combo_input_buffer_seconds
// and auto-fires when the cancel window opens. Mashed presses that
// arrive too early are dropped without being eaten visually.
struct BufferedPress
{
    bool pending = false;
    AttackKind kind = AttackKind::Light;
    // "LMB" / "RMB" — replayed when the buffered press fires so the
    // technique-locking logic sees the same input as a live press.
    const char* button = "LMB";
    float buffered_at = 0.0f; // wall-clock; expires after combo_input_buffer_seconds
};
static BufferedPress sBufferedRight;
static BufferedPress sBufferedLeft;

// First-press latch out of Peaceful stance. Fires after entry delay
// so locomotion can crossfade from standard_idle into combat-idle
// before the action plays.
enum class PendingFirstActionKind
{
    Attack,
    Block,
};
struct PendingFirstAction
{
    bool active = false;
    PendingFirstActionKind kind = PendingFirstActionKind::Attack;
    selva::combat::HandSide hand = selva::combat::HandSide::Right;
    AttackKind attack_kind = AttackKind::Light; // attack only
    // Button that triggered the latch. Replayed through dispatch when
    // the establishing beat elapses so technique-locking sees the same
    // input a live press would have.
    const char* button = "LMB";
    const char* block_clip = nullptr;           // block only
    bool block_freeze_last = false;             // block only
    float fire_at = 0.0f;
};
static PendingFirstAction sPendingFirstAction;

// Wall-clock seconds since the game started — owned by
// selva::wallClock() (WallClock.h). Advanced once per frame at the
// top of selvaPerFrame via selva::advanceWallClock(dt).

// Wall-clock time at which post-attack locomotion lockout ends. Set on
// each attack fire to "now + effective clip duration + grace". While
// in the future, the SM forces is_moving = false so WASD smashing
// during attack recovery can't ping-pong loco clips and produce leg
// spasm. Independent of CombatReady stance — pure F-toggle stance
// without an attack does NOT lock movement.
static float sLocoLockoutUntil = 0.0f;

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
// Resolved attack: the WeaponAttack record from the data model + the
// loaded clip pointer. `chain_size` is how many entries are in the
// chain that this attack came from (so the caller knows whether
// stepping to chain_index+1 is valid). nullptrs indicate "no swing
// this frame."
struct ResolvedAttack
{
    const selva::anim::AnimationClip* clip = nullptr;
    const selva::combat::WeaponAttack* attack = nullptr;
    int chain_size = 0;
};

// Does the off-hand weapon support held-block? Today the rule is
// "yes if the weapon's class id is 'buckler'." Future: promote to a
// per-WeaponClass capability flag (e.g. `can_block: true` in the
// JSON) so swords with buckler-style parries, large shields, etc.
// can opt in without hardcoded class names.
static bool offHandCanBlock(const selva::combat::PlayerEquipment& eq)
{
    const selva::combat::Weapon* w = eq.left;
    if (w == nullptr || w->cls == nullptr)
        return false;
    return w->cls->id == "buckler";
}

// True when neither hand holds a real weapon (both empty or both fists).
static bool isUnarmed(const selva::combat::PlayerEquipment& eq)
{
    auto is_fists = [](const selva::combat::Weapon* w)
    { return w == nullptr || (w->cls != nullptr && w->cls->id == "unarmed"); };
    return is_fists(eq.right) && is_fists(eq.left);
}

// Pick the technique-list for (grip, kind), select the `technique_index`
// technique, return its `chain_index`-th attack resolved against the
// clip registry. Falls back: empty Running list → use Light instead;
// out-of-range technique_index → use 0; missing clip → nullptr clip.
static ResolvedAttack resolveAttackChainEntry(const selva::combat::PlayerEquipment& eq,
                                              selva::combat::HandSide hand, AttackKind kind,
                                              int chain_index, int technique_index)
{
    using selva::combat::Grip;
    using selva::combat::HandSide;
    ResolvedAttack out;
    const selva::combat::Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return out;
    const auto& aset = (eq.grip == Grip::TwoHanded) ? w->cls->two_handed : w->cls->one_handed;

    const auto* techniques = &aset.light;
    if (kind == AttackKind::Heavy)
        techniques = &aset.heavy;
    else if (kind == AttackKind::Running && !aset.running.empty())
        techniques = &aset.running;

    if (techniques->empty())
        return out;
    const int t_idx =
        (technique_index >= 0 && technique_index < static_cast<int>(techniques->size()))
            ? technique_index
            : 0;
    const auto& chain = (*techniques)[t_idx].attacks;
    if (chain.empty())
        return out;

    out.chain_size = static_cast<int>(chain.size());
    const int idx = (chain_index >= 0 && chain_index < out.chain_size) ? chain_index : 0;
    out.attack = &chain[idx];
    if (out.attack->clip.empty())
        return out;
    out.clip = sClips.get(out.attack->clip);
    return out;
}

// Result of dispatching a button press to the technique list. `locked`
// is the technique_index to commit (>=0) or -1 to keep the chain
// unlocked (multiple techniques still match). `valid` is false when no
// technique accepts the press at the given step — caller treats that
// as a chain miss.
struct TechniqueDispatch
{
    int locked = -1;
    bool valid = false;
};

// A WeaponAttack's expected_button matches a press button if the field
// is empty, "any", or equal to the press button (case-insensitive on the
// limited set of values we accept).
static bool buttonMatches(const std::string& expected, const char* press)
{
    if (expected.empty() || expected == "any")
        return true;
    return expected == press;
}

// Pick which technique to commit to (if any) given the press button at
// step `chain_index`. When already locked, just validate that the
// locked technique accepts this button.
static TechniqueDispatch dispatchTechniqueForPress(const selva::combat::PlayerEquipment& eq,
                                                   selva::combat::HandSide hand, AttackKind kind,
                                                   int chain_index, int current_locked,
                                                   const char* button)
{
    using selva::combat::Grip;
    using selva::combat::HandSide;
    TechniqueDispatch out;
    const selva::combat::Weapon* w = (hand == HandSide::Right) ? eq.right : eq.left;
    if (w == nullptr || w->cls == nullptr)
        return out;
    const auto& aset = (eq.grip == Grip::TwoHanded) ? w->cls->two_handed : w->cls->one_handed;
    const auto* techniques = &aset.light;
    if (kind == AttackKind::Heavy)
        techniques = &aset.heavy;
    else if (kind == AttackKind::Running && !aset.running.empty())
        techniques = &aset.running;
    if (techniques->empty())
        return out;

    // Already locked: just check the locked technique accepts this press.
    if (current_locked >= 0 && current_locked < static_cast<int>(techniques->size()))
    {
        const auto& chain = (*techniques)[current_locked].attacks;
        if (chain_index < 0 || chain_index >= static_cast<int>(chain.size()))
            return out; // past the end of the locked chain
        if (!buttonMatches(chain[chain_index].expected_button, button))
            return out;
        out.locked = current_locked;
        out.valid = true;
        return out;
    }

    // Unlocked: gather candidates whose chain[chain_index] accepts the
    // press button. If exactly one matches, lock it. If many match,
    // leave unlocked (caller fires whichever — typically index 0 — and
    // the locking happens on the next press).
    int last_match = -1;
    int match_count = 0;
    for (std::size_t i = 0; i < techniques->size(); ++i)
    {
        const auto& chain = (*techniques)[i].attacks;
        if (chain_index < 0 || chain_index >= static_cast<int>(chain.size()))
            continue;
        if (buttonMatches(chain[chain_index].expected_button, button))
        {
            last_match = static_cast<int>(i);
            ++match_count;
        }
    }
    if (match_count == 0)
        return out;
    out.valid = true;
    out.locked = (match_count == 1) ? last_match : -1;
    return out;
}

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
static glm::vec3 sLastRHWorldPos = glm::vec3(0.0f);
static bool sLastRHValid = false;

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

// Path to the live tunables config, relative to the working directory.
static const std::string kTunablesPath = "config/tunables.json";
static const std::string kWeaponClassesDir = "config/weapon_classes";
static const std::string kWeaponsDir = "config/weapons";
static const std::string kLoadoutPath = "config/loadout.json";

// Combat data registries — loaded once at startup from JSON, queried for
// the rest of the run. WeaponClass holds animation/attach data shared
// across all weapons of a kind (sword, dagger, ...); Weapon holds per-
// weapon stats and a pointer into the class registry. PlayerEquipment
// references the registry — registry must outlive equipment.
static selva::combat::WeaponClassRegistry sWeaponClasses;
static selva::combat::WeaponRegistry sWeapons;
static selva::combat::PlayerEquipment sEquipment;

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
static float poseMatchStartFromLoco(const selva::anim::AnimationClip& new_clip,
                                    float window_seconds)
{
    const auto* loco_clip = sClips.get(sLastLocoClipName);
    if (loco_clip == nullptr || !loco_clip->isLoaded())
        return 0.0f;
    // Match on hands AND feet. Hands-only would match roll/backstep
    // clips at t=0 because their t=0 hand pose is similar to combat-
    // idle's hand pose, while the LEG pose at t=0 is wildly different
    // (weight pre-shifted for the dodge motion). Including feet in
    // the score weights the scan toward frames where the legs also
    // match.
    std::vector<int> joints;
    for (const char* name :
         {"mixamorig:RightHand", "mixamorig:LeftHand", "mixamorig:RightFoot", "mixamorig:LeftFoot"})
    {
        const int idx = sSampler.findJoint(name);
        if (idx >= 0)
            joints.push_back(idx);
    }
    if (joints.empty())
        return 0.0f;
    const auto fd = sSampler.frameDiagnostics();
    return sSampler.clipPoseMatchTime(*loco_clip, fd.loco_current_time, new_clip, joints, 0.0f,
                                      window_seconds);
}

// True when the locomotion track is currently a moving (gait) clip
// rather than a stance idle. Drives establish-beat decisions, walk-
// source bookend dispatch for dodge, and other "where is the body
// right now" checks. Centralized so the moving-clip set has one
// definition.
static bool isMovingLocoClip(const std::string& name)
{
    return name == "walking" || name == "running" || name == "run_to_stop";
}

// Five-joint splice diagnostic. Captured pre-playOneShot, logged post.
// The same pattern was duplicated in fireAttack, fireBlock, and the
// dodge-fire path — extracted so all three log identical columns.
struct SpliceDiag
{
    int rh = -1, lh = -1, hp = -1, lf = -1, rf = -1;
    glm::vec3 live_rh{}, live_lh{}, live_hp{}, live_lf{}, live_rf{};
};

static SpliceDiag captureSpliceDiag()
{
    SpliceDiag d;
    d.rh = sSampler.findJoint("mixamorig:RightHand");
    d.lh = sSampler.findJoint("mixamorig:LeftHand");
    d.hp = sSampler.findJoint("mixamorig:Hips");
    d.lf = sSampler.findJoint("mixamorig:LeftFoot");
    d.rf = sSampler.findJoint("mixamorig:RightFoot");
    if (d.rh >= 0)
        d.live_rh = sSampler.jointWorldPos(d.rh);
    if (d.lh >= 0)
        d.live_lh = sSampler.jointWorldPos(d.lh);
    if (d.hp >= 0)
        d.live_hp = sSampler.jointWorldPos(d.hp);
    if (d.lf >= 0)
        d.live_lf = sSampler.jointWorldPos(d.lf);
    if (d.rf >= 0)
        d.live_rf = sSampler.jointWorldPos(d.rf);
    return d;
}

// Sample each captured joint at `start_seconds` of `new_clip` and
// log the joint-by-joint distance from live to entry. Caller decides
// what header tag to use (e.g. "[combat:dodge-fire ...]").
static void logSpliceDiag(const SpliceDiag& d, const selva::anim::AnimationClip& new_clip,
                          float start_seconds, const char* prefix)
{
    auto entry = [&](int j) {
        return (j >= 0) ? sSampler.sampleJointWorldPos(new_clip, start_seconds, j) : glm::vec3(0);
    };
    combatLog("%s  RH=%.3fm LH=%.3fm Hip=%.3fm LFoot=%.3fm RFoot=%.3fm\n", prefix,
              glm::length(entry(d.rh) - d.live_rh), glm::length(entry(d.lh) - d.live_lh),
              glm::length(entry(d.hp) - d.live_hp), glm::length(entry(d.lf) - d.live_lf),
              glm::length(entry(d.rf) - d.live_rf));
}

// Resolve the playback rate for the given hand's weapon class. Class
// override (>0) wins over the global tunable.
static float effectiveAttackPlaybackRate(selva::combat::HandSide hand)
{
    const float global = selva::tuning::current().attack_playback_rate;
    const selva::combat::Weapon* w =
        (hand == selva::combat::HandSide::Right) ? sEquipment.right : sEquipment.left;
    if (w != nullptr && w->cls != nullptr && w->cls->attack_playback_rate > 0.0f)
        return w->cls->attack_playback_rate;
    return global;
}

// ---------------------------------------------------------------------------
// TransitionProfile: one bag of decisions per "fire a one-shot" call site.
//
// The animation system has 6 axes of choice at every transition (dodge,
// attack, block, future jump). Hard-coding those axes inline at each
// call site makes adding/changing a transition risk regressing the
// others, and makes consistency between transitions invisible. A
// profile makes every choice explicit, so call sites read like:
//   fireOneShot(clip, kProfileSprintFinisher, ...);
//
// One profile per transition class. Add new transitions by adding a
// new constant; never copy-paste the call-site scaffolding.
// ---------------------------------------------------------------------------

struct TransitionProfile
{
    enum class SourcePrep
    {
        None,             // capture whatever's on screen
        SnapLocoToZero,   // setLocomotionClipTime(0) before fire (block-from-CombatReady)
    };

    enum class Lockout
    {
        None,                // no walking-suppression (block, dodge — dodge has its own gate)
        CancelWindowClose,   // sLocoLockoutUntil = chain.cancel_window_close_at + extension
        WallClockSeconds,    // sLocoLockoutUntil = now + lockout_seconds (sprint-finisher)
    };

    float blend_in_seconds = 0.20f;
    float blend_out_seconds = 0.20f;
    SourcePrep source_prep = SourcePrep::None;
    bool enroll_inertialization = true;
    Lockout lockout = Lockout::None;
    float lockout_seconds = 0.0f; // meaning depends on Lockout
    selva::anim::PoseSampler::BodyMask mask =
        selva::anim::PoseSampler::BodyMask::Full;
    bool freeze_last = false;
};

// Pre-built profiles for every transition class in the game today.
// Add a new transition? Add a constant. Never inline these settings.
namespace profiles
{
// Cold attack from combat-idle. Pose-match against the loco track
// (caller supplies start_seconds from that scan). Inertialization
// smooths the residual offset.
inline TransitionProfile firstStrike()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.first_strike_blend_seconds;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}
// Mid-chain attack splice. Caller supplies start_seconds from
// pose-match against the OUTGOING one-shot, or the clip's authored
// chain_link_start_seconds.
inline TransitionProfile chainLink()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.combo_chain_blend_seconds;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::CancelWindowClose;
    return p;
}
// Sprint-LMB → running attack (flying knee, etc). Live running pose
// ≈ clip t=0; offset is small. No inertialization (would capture
// mid-running pose and apply it on top of the running clip — visible
// spasm on rapid-fire). Lockout in wall-clock seconds.
inline TransitionProfile sprintFinisher()
{
    const auto& tun = selva::tuning::current();
    TransitionProfile p;
    p.blend_in_seconds = tun.sprint_finisher_blend_in_seconds;
    p.enroll_inertialization = false;
    p.lockout = TransitionProfile::Lockout::WallClockSeconds;
    p.lockout_seconds = tun.sprint_finisher_lockout_seconds;
    return p;
}
// Block fired through the first-action latch (locomotion already
// settled into combat-idle).
inline TransitionProfile blockFromLatch()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}
// Block fired live from CombatReady. Snap loco to t=0 first so the
// splice has a known handoff frame.
inline TransitionProfile blockLive()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::SnapLocoToZero;
    p.enroll_inertialization = true;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}
// Dodge (roll / backstep). No inertialization — dodge commits and
// has its own movement gate (sDodgeActive); a captured-offset
// overlay would just add visible motion on top of the running clip.
inline TransitionProfile dodge()
{
    TransitionProfile p;
    p.blend_in_seconds = 0.10f;
    p.source_prep = TransitionProfile::SourcePrep::None;
    p.enroll_inertialization = false;
    p.lockout = TransitionProfile::Lockout::None;
    return p;
}
} // namespace profiles

// Single entry point for "fire a one-shot with this profile." Reads
// profile, applies source-prep, optionally enrolls inertialization,
// kicks playOneShot. Lockout assignment happens at the caller because
// the lockout target's anchor (cancel_window_close_at, dodge end,
// etc.) belongs to caller-specific state.
//
// `start_seconds` is computed by the caller via whatever strategy
// fits — pose-match, authored offset, leading-idle trim, or just 0.
// The profile is dumb about start_seconds; the caller is responsible.
static void fireOneShotWithProfile(const selva::anim::AnimationClip& clip,
                                   const TransitionProfile& profile, float start_seconds,
                                   float playback_rate)
{
    if (profile.source_prep == TransitionProfile::SourcePrep::SnapLocoToZero)
        sSampler.setLocomotionClipTime(0.0f);
    if (profile.enroll_inertialization)
        sSampler.requestInertialization(profile.blend_in_seconds);
    sSampler.playOneShot(clip, profile.blend_in_seconds, profile.blend_out_seconds, profile.mask,
                         start_seconds, playback_rate, profile.freeze_last);
}

// Apply a profile's lockout strategy. Called by chain code after
// chain.cancel_window_close_at is computed for the just-fired attack.
static void applyProfileLockout(const TransitionProfile& profile,
                                float cancel_window_close_at)
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

// Window size (read at init for the projection's aspect ratio; updated on
// resize via the engine onResize callback).
static int sWindowW = 0;
static int sWindowH = 0;

static void onWindowResize(Engine& /*engine*/, int new_w, int new_h)
{
    sWindowW = new_w;
    sWindowH = new_h;
}

// ---------------------------------------------------------------------------
// Yaw helpers
// ---------------------------------------------------------------------------

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
// Used by smooth turn-to-direction logic; if we're at 170° and target is
// -170°, the unwrapped delta is -340° (going the long way), but the wrapped
// delta is +20° (going the short way through 180°).
static float wrapAngleSigned(float delta)
{
    while (delta > glm::pi<float>())
        delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>())
        delta += glm::two_pi<float>();
    return delta;
}

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z) is
// the inverse of (sin(yaw), -, -cos(yaw))-style forward-vector formulas.
static float yawFromGroundDir(const glm::vec3& dir)
{
    return std::atan2(-dir.x, -dir.z);
}

// Resolve every loaded weapon class's per-attack cancel-open time. For
// each WeaponAttack, either honor the JSON `cancel_open_seconds`
// override (if non-negative) or derive it from a clip-scan via
// PoseSampler::clipJointMotionEnd(). The watched joints come from the
// attack's `motion_joints` field, falling back to a grip-default set
// (right hand for one-handed, both hands for two-handed) when the
// override list is empty.
//
// Run once at startup, after sSampler/sClips are initialized and after
// sWeaponClasses has loaded JSON. Mutates each WeaponAttack's
// `resolved_cancel_open_seconds`. fireAttack reads that field at runtime.
static void resolveAttackCancelOpenTimes()
{
    auto resolve_joints = [&](const std::vector<std::string>& names,
                              bool two_handed) -> std::vector<int>
    {
        std::vector<int> out;
        if (!names.empty())
        {
            out.reserve(names.size());
            for (const auto& n : names)
            {
                const int idx = sSampler.findJoint(n.c_str());
                if (idx >= 0)
                    out.push_back(idx);
            }
            return out;
        }
        // Grip default: right hand for one-handed, both hands for
        // two-handed. Both Mixamo conventions.
        const int rh = sSampler.findJoint("mixamorig:RightHand");
        if (rh >= 0)
            out.push_back(rh);
        if (two_handed)
        {
            const int lh = sSampler.findJoint("mixamorig:LeftHand");
            if (lh >= 0)
                out.push_back(lh);
        }
        return out;
    };

    const float velocity_frac = selva::tuning::current().cancel_open_velocity_fraction;
    auto resolve_chain = [&](std::vector<selva::combat::WeaponAttack>& chain, bool two_handed)
    {
        // Pass 1: cancel-open and joint-velocity-derived motion-start
        // per attack (independent of neighbors).
        std::vector<std::vector<int>> joints_per_attack;
        joints_per_attack.reserve(chain.size());
        for (auto& atk : chain)
        {
            const auto* clip = sClips.get(atk.clip);
            if (clip == nullptr || !clip->isLoaded())
            {
                atk.resolved_cancel_open_seconds = 0.0f;
                atk.resolved_chain_link_start_seconds = 0.0f;
                joints_per_attack.emplace_back();
                continue;
            }
            const auto joints = resolve_joints(atk.motion_joints, two_handed);
            joints_per_attack.push_back(joints);
            if (atk.cancel_open_seconds >= 0.0f)
                atk.resolved_cancel_open_seconds =
                    std::min(atk.cancel_open_seconds, clip->duration());
            else
                atk.resolved_cancel_open_seconds =
                    sSampler.clipJointMotionEnd(*clip, joints, 60.0f, velocity_frac);
            // Chain-link start. Two cases:
            //   * JSON override (chain_link_start_seconds >= 0): trust
            //     the author. Used for clips that have a Blender-
            //     authored bookend in their first ~5-10 frames; we
            //     want to enter at frame 0 so the bookend pose is
            //     what the player sees. Without this override the
            //     auto-detect's motion_start scan would skip past
            //     the bookend (because the bookend is a static-
            //     ish pose region with no motion to detect).
            //   * Default (no override): motion_start - 0.05s, the
            //     original "skip authored windup" behavior. Right for
            //     unmodified Mixamo clips whose first ~0.3s is dead
            //     pose-rest before the swing kicks in.
            if (atk.chain_link_start_seconds >= 0.0f)
            {
                atk.resolved_chain_link_start_seconds =
                    std::min(atk.chain_link_start_seconds, clip->duration());
            }
            else
            {
                const float motion_start = sSampler.clipJointMotionStart(*clip, joints);
                atk.resolved_chain_link_start_seconds =
                    std::clamp(motion_start - 0.05f, 0.0f, clip->duration());
            }
        }

        // Pass 2 (velocity-direction matching) — REVERTED.
        //
        // Diagnostic confirmed velocity-matching brought pre↔post
        // angle from 135° → 9° on the jerky 1→3 transition (great)
        // but decay↔post (the angle of the position-offset relative
        // to motion) stayed near 100° — meaning inertialization
        // still smears sideways across the motion arc. Visible
        // result: 1→3 wasn't noticeably better, and 2→3 (which was
        // already perfect) shifted to a later chain_start that
        // made slash_3's recovery look cut off.
        //
        // Conclusion: directional alignment is necessary but not
        // sufficient. Closing the residual position offset requires
        // either asset-side bookend re-pose (Blender) or full
        // motion-matching (multi-clip pose database) — neither
        // achievable with the velocity-only signal at runtime.
        // The simple Pass-1 splice (motion_start - 0.05s) is the
        // best available given the assets; Pass 2 is left here as
        // a comment because it was a real diagnostic step, not a
        // bandaid.
    };

    auto log_chain = [&](const char* class_id, const char* slot,
                         const std::vector<selva::combat::WeaponAttack>& chain, bool two_handed)
    {
        if (chain.empty())
            return;
        combatLog("[combat:resolve] %s/%s chain (%zu entries):\n", class_id, slot, chain.size());
        for (std::size_t i = 0; i < chain.size(); ++i)
        {
            const auto& atk = chain[i];
            const auto* clip = sClips.get(atk.clip);
            const float dur = (clip != nullptr && clip->isLoaded()) ? clip->duration() : 0.0f;
            const float open = atk.resolved_cancel_open_seconds;
            const float start = atk.resolved_chain_link_start_seconds;
            const float open_frac = (dur > 0.0f) ? (open / dur) : 0.0f;
            const float start_frac = (dur > 0.0f) ? (start / dur) : 0.0f;
            const char* role = (i == 0) ? "first" : "chain-link";
            float motion_start = 0.0f;
            float motion_peak = 0.0f;
            if (clip != nullptr && clip->isLoaded())
            {
                const auto joints = resolve_joints(atk.motion_joints, two_handed);
                motion_start = sSampler.clipJointMotionStart(*clip, joints);
                motion_peak = sSampler.clipJointMotionPeak(*clip, joints);
            }
            const float ms_frac = (dur > 0.0f) ? (motion_start / dur) : 0.0f;
            const float mp_frac = (dur > 0.0f) ? (motion_peak / dur) : 0.0f;
            combatLog("  [%zu] %-30s dur=%.3fs  motion_start=%.3fs (%.0f%%)  "
                      "peak=%.3fs (%.0f%%)  cancel_open=%.3fs (%.0f%%)  "
                      "chain_start=%.3fs (%.0f%%)  role=%s\n",
                      i, atk.clip.c_str(), dur, motion_start, ms_frac * 100.0f, motion_peak,
                      mp_frac * 100.0f, open, open_frac * 100.0f, start, start_frac * 100.0f, role);
        }
    };

    // One-off: compare slash_3 vs slash_3_reposed past the blend
    // region at several time points and across multiple joints
    // (hands, hips, AND legs). If any joint diverges outside
    // [0, 0.27s] (the 8-frame blend zone at 30fps), Blender's export
    // corrupted frames it shouldn't have touched.
    {
        const auto* orig = sClips.get("sword_and_shield_slash_3");
        const auto* rep = sClips.get("sword_and_shield_slash_3_reposed");
        const int rh = sSampler.findJoint("mixamorig:RightHand");
        const int lh = sSampler.findJoint("mixamorig:LeftHand");
        const int hp = sSampler.findJoint("mixamorig:Hips");
        const int lf = sSampler.findJoint("mixamorig:LeftFoot");
        const int rf = sSampler.findJoint("mixamorig:RightFoot");
        const int lul = sSampler.findJoint("mixamorig:LeftUpLeg");
        const int rul = sSampler.findJoint("mixamorig:RightUpLeg");
        if (orig != nullptr && orig->isLoaded() && rep != nullptr && rep->isLoaded() && rh >= 0)
        {
            combatLog("[combat:reposed_diff] comparing slash_3 vs slash_3_reposed past blend "
                      "region (blend ends ~0.27s):\n");
            auto joint_delta = [&](const selva::anim::AnimationClip& a,
                                   const selva::anim::AnimationClip& b, float t,
                                   int joint) -> float
            {
                if (joint < 0)
                    return 0.0f;
                return glm::length(sSampler.sampleJointWorldPos(b, t, joint) -
                                   sSampler.sampleJointWorldPos(a, t, joint));
            };
            for (float t : {0.05f, 0.20f, 0.30f, 0.50f, 0.80f, 1.00f, 1.30f, 1.50f})
            {
                combatLog("  t=%.2fs  RH=|%.3fm|  LH=|%.3fm|  Hip=|%.3fm|  LFoot=|%.3fm|  "
                          "RFoot=|%.3fm|  LUpLeg=|%.3fm|  RUpLeg=|%.3fm|\n",
                          t, joint_delta(*orig, *rep, t, rh), joint_delta(*orig, *rep, t, lh),
                          joint_delta(*orig, *rep, t, hp), joint_delta(*orig, *rep, t, lf),
                          joint_delta(*orig, *rep, t, rf), joint_delta(*orig, *rep, t, lul),
                          joint_delta(*orig, *rep, t, rul));
            }
        }
    }

    auto log_bookend_alignment = [&](const char* class_id, const char* slot,
                                     const std::vector<selva::combat::WeaponAttack>& chain)
    {
        // For each (prev, next) pair, sample prev's RH at prev's
        // cancel_open_seconds and next's RH at next's chain_start.
        // The delta between these is the bookend gap the asset author
        // (Blender re-pose) is responsible for closing. Independent of
        // player-input-timing variance — this is what the resolver
        // SEES, not what the live game shows.
        const int rh = sSampler.findJoint("mixamorig:RightHand");
        if (rh < 0)
            return;
        for (std::size_t i = 1; i < chain.size(); ++i)
        {
            const auto& prev = chain[i - 1];
            const auto& cur = chain[i];
            const auto* p_clip = sClips.get(prev.clip);
            const auto* c_clip = sClips.get(cur.clip);
            if (p_clip == nullptr || !p_clip->isLoaded() || c_clip == nullptr ||
                !c_clip->isLoaded())
                continue;
            const glm::vec3 prev_rh =
                sSampler.sampleJointWorldPos(*p_clip, prev.resolved_cancel_open_seconds, rh);
            const glm::vec3 cur_rh =
                sSampler.sampleJointWorldPos(*c_clip, cur.resolved_chain_link_start_seconds, rh);
            const glm::vec3 d = cur_rh - prev_rh;
            combatLog("[combat:bookend] %s/%s [%zu->%zu]  prev=%s @ %.3fs RH=(%.3f,%.3f,%.3f)  "
                      "next=%s @ %.3fs RH=(%.3f,%.3f,%.3f)  delta=(%+.3f,%+.3f,%+.3f) |%.3fm|\n",
                      class_id, slot, i - 1, i, prev.clip.c_str(),
                      prev.resolved_cancel_open_seconds, prev_rh.x, prev_rh.y, prev_rh.z,
                      cur.clip.c_str(), cur.resolved_chain_link_start_seconds, cur_rh.x, cur_rh.y,
                      cur_rh.z, d.x, d.y, d.z, glm::length(d));
        }
    };

    auto resolve_techniques = [&](std::vector<selva::combat::WeaponTechnique>& techs,
                                  bool two_handed)
    {
        for (auto& t : techs)
            resolve_chain(t.attacks, two_handed);
    };

    for (auto& kv : sWeaponClasses.by_id)
    {
        auto& cls = kv.second;
        resolve_techniques(cls.one_handed.light, false);
        resolve_techniques(cls.one_handed.heavy, false);
        resolve_techniques(cls.one_handed.running, false);
        resolve_techniques(cls.two_handed.light, true);
        resolve_techniques(cls.two_handed.heavy, true);
        resolve_techniques(cls.two_handed.running, true);

        // Auto-detect block clip's leading-idle trim if not overridden.
        // Only unarmed has its own block clip currently; sword uses
        // sword_and_shield_block which is short and authored without
        // a leading idle, so trim defaults to 0 there.
        if (cls.block_clip_start_seconds < 0.0f)
        {
            const char* block_clip_name = (cls.id == "unarmed") ? "unarmed_block" : nullptr;
            if (block_clip_name != nullptr)
            {
                const auto* bclip = sClips.get(block_clip_name);
                if (bclip != nullptr && bclip->isLoaded())
                {
                    const int rh = sSampler.findJoint("mixamorig:RightHand");
                    const int lh = sSampler.findJoint("mixamorig:LeftHand");
                    std::vector<int> joints;
                    if (rh >= 0)
                        joints.push_back(rh);
                    if (lh >= 0)
                        joints.push_back(lh);
                    const float motion_start = sSampler.clipJointMotionStart(*bclip, joints);
                    cls.block_clip_start_seconds =
                        std::clamp(motion_start - 0.05f, 0.0f, bclip->duration());
                    combatLog("[combat:resolve] %s block trim: motion_start=%.3fs -> "
                              "block_clip_start=%.3fs\n",
                              cls.id.c_str(), motion_start, cls.block_clip_start_seconds);
                }
            }
        }
        auto log_techniques = [&](const char* slot,
                                  const std::vector<selva::combat::WeaponTechnique>& techs,
                                  bool two_handed)
        {
            for (std::size_t ti = 0; ti < techs.size(); ++ti)
            {
                char label[64];
                std::snprintf(label, sizeof(label), "%s [%zu:%s]", slot, ti,
                              techs[ti].id.c_str());
                log_chain(cls.id.c_str(), label, techs[ti].attacks, two_handed);
                log_bookend_alignment(cls.id.c_str(), label, techs[ti].attacks);
            }
        };
        log_techniques("1H light", cls.one_handed.light, false);
        log_techniques("1H heavy", cls.one_handed.heavy, false);
        log_techniques("1H running", cls.one_handed.running, false);
        log_techniques("2H light", cls.two_handed.light, true);
        log_techniques("2H heavy", cls.two_handed.heavy, true);
        log_techniques("2H running", cls.two_handed.running, true);
    }
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
        sCamYaw -= static_cast<float>(mdx) * tun.mouse_sensitivity;
        sCamPitch -= static_cast<float>(mdy) * tun.mouse_sensitivity;
        if (sCamPitch < tun.pitch_min)
            sCamPitch = tun.pitch_min;
        if (sCamPitch > tun.pitch_max)
            sCamPitch = tun.pitch_max;
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

    // Auto-reset chains that have aged past their grace window. This
    // converts "I attacked once and walked away" into "next press is
    // a fresh slash 1," not "next press is slash 2 of a stale chain."
    // Also reset the rhythm-window timestamps so the next press is
    // treated as a fresh first-strike (in_window true).
    auto resetChain = [](AttackChainState& chain)
    {
        chain.chain_index = 0;
        chain.technique_index = -1;
        chain.cancel_window_open_at = 0.0f;
        chain.cancel_window_close_at = 0.0f;
        chain.chain_reset_at = 0.0f;
        chain.is_finisher = false;
        chain.last_press_accuracy = 0.0f;
        chain.last_press_was_perfect = false;
    };
    if (sChainRight.chain_index > 0 && selva::wallClock() >= sChainRight.chain_reset_at)
        resetChain(sChainRight);
    if (sChainLeft.chain_index > 0 && selva::wallClock() >= sChainLeft.chain_reset_at)
        resetChain(sChainLeft);
    // Even after the chain wraps to 0 (mid-flight, after firing the
    // final entry), the window timestamps are still set forward by the
    // last fire. Once the reset grace passes, those need clearing too
    // so the next press is a fresh first-strike, not "past the close
    // of slash_4's window."
    if (sChainRight.chain_index == 0 && sChainRight.cancel_window_open_at > 0.0f &&
        selva::wallClock() >= sChainRight.chain_reset_at)
        resetChain(sChainRight);
    if (sChainLeft.chain_index == 0 && sChainLeft.cancel_window_open_at > 0.0f &&
        selva::wallClock() >= sChainLeft.chain_reset_at)
        resetChain(sChainLeft);

    // Expire stale buffered presses.
    if (sBufferedRight.pending &&
        selva::wallClock() - sBufferedRight.buffered_at >= tun.combo_input_buffer_seconds)
        sBufferedRight.pending = false;
    if (sBufferedLeft.pending &&
        selva::wallClock() - sBufferedLeft.buffered_at >= tun.combo_input_buffer_seconds)
        sBufferedLeft.pending = false;

    // Fire an attack on the given hand at the chain's current step.
    // Updates the chain index, cancel-window, and reset-grace timers
    // so the next press logic can answer "are we mid-window? past
    // window? past chain?" Returns true if the fire actually played
    // a clip.
    auto fireAttack = [&](selva::combat::HandSide hand, AttackKind kind) -> bool
    {
        AttackChainState& chain =
            (hand == selva::combat::HandSide::Right) ? sChainRight : sChainLeft;
        // A change in attack kind resets the chain — light and
        // heavy are their own chains, no cross-mixing.
        if (chain.chain_index > 0 && chain.chain_kind != kind)
        {
            chain.chain_index = 0;
            chain.technique_index = -1;
            chain.is_finisher = false;
        }
        // Resolver picks the technique slot. -1 = caller hasn't
        // locked one yet → use index 0 (the first technique).
        const int t_idx = (chain.technique_index >= 0) ? chain.technique_index : 0;
        ResolvedAttack ra = resolveAttackChainEntry(sEquipment, hand, kind, chain.chain_index,
                                                    t_idx);
        if (ra.clip == nullptr || !ra.clip->isLoaded() || ra.attack == nullptr)
            return false;
        // Sprint swap: while sprinting, ANY light attack press fires
        // the running technique's clip (e.g. flying_knee_punch_combo)
        // instead of the locked Light technique's current step. The
        // running entry is a single-step chain that commits as a
        // finisher — it's a self-contained sprint attack that doesn't
        // chain into anything. This handles both "cold sprint + LMB"
        // and "mid-chain + start sprinting + LMB" the same way: drop
        // the regular chain, fire the running clip, treat it as the
        // chain's end. Sword-class running slot gets the same
        // treatment uniformly.
        bool is_sprint_swap = false;
        if (sPlayer.sprinting && kind == AttackKind::Light)
        {
            const selva::combat::Weapon* w = (hand == selva::combat::HandSide::Right)
                                                 ? sEquipment.right
                                                 : sEquipment.left;
            if (w != nullptr && w->cls != nullptr)
            {
                const auto& aset = (sEquipment.grip == selva::combat::Grip::TwoHanded)
                                       ? w->cls->two_handed
                                       : w->cls->one_handed;
                if (!aset.running.empty() && !aset.running[0].attacks.empty())
                {
                    const auto& running_atk = aset.running[0].attacks[0];
                    const auto* running_clip = sClips.get(running_atk.clip);
                    if (running_clip != nullptr && running_clip->isLoaded())
                    {
                        ra.attack = &running_atk;
                        ra.clip = running_clip;
                        ra.chain_size = 1; // forces was_last=true, finisher commits
                        // chain.chain_index already at the final slot —
                        // leave it; the bump-and-wrap below will mark
                        // is_finisher and reset to 0 cleanly.
                        chain.chain_index = 0; // single-step running chain
                        is_sprint_swap = true;
                        combatLog("[combat:fire] sprint finisher swap → %s\n",
                                  running_atk.clip.c_str());
                    }
                }
            }
        }
        // Every attack fires full-body. The previous walking → upper-
        // body-mask path produced a leg cycle running underneath the
        // attack's authored leg motion (hip pivot, weight transfer),
        // which read as a leg spasm — the punch's hip drive and the
        // walk loop's hip cycle fight each other. Player plants, swings,
        // resumes walking when the one-shot blends out.
        const selva::anim::PoseSampler::BodyMask mask =
            selva::anim::PoseSampler::BodyMask::Full;
        // Chain transitions use a different blend-source than first-
        // strike. Two distinct cases:
        //
        //   * First-strike (chain_index == 0): start the clip at t=0
        //     (full windup is visible) and inertialize from the
        //     canonical combat-stance baseline. The previous on-screen
        //     pose is peaceful idle / walking / running — different
        //     pose family from the attack's authored t=0 — so capturing
        //     screen-pose would produce leg-spaz. Baseline-pose
        //     captures avoid that.
        //
        //   * Chain link (chain_index > 0): skip past the clip's
        //     authored windup and start at resolved_chain_link_start_
        //     seconds (the clip's first significant joint motion,
        //     minus a 0.05s back-off). Inertialize from the LIVE
        //     screen pose, NOT the combat-stance baseline. Rationale:
        //     the previous swing's recovery left the body somewhere
        //     close to mid-swing pose; the new clip ALSO starts mid-
        //     swing; the offset is small. Capturing screen-pose
        //     bridges the small remaining gap. Baseline-pose would
        //     re-introduce a "snap back to combat-stance, then play
        //     forward" artifact (the cut-off-and-restart symptom).
        const bool first_strike = (chain.chain_index == 0);
        // Per-attack blend override (chain_link_blend_seconds >= 0)
        // wins over the global tun.combo_chain_blend_seconds. Lets
        // each chain-link transition be tuned independently — a
        // large pose-mismatch transition can have a longer decay
        // window without dragging out the easier ones.
        const float per_attack_blend = ra.attack->chain_link_blend_seconds;
        const float chain_blend =
            (per_attack_blend >= 0.0f) ? per_attack_blend : tun.combo_chain_blend_seconds;
        const float blend_in = first_strike ? tun.first_strike_blend_seconds : chain_blend;

        // Sprint-finisher uses a fixed start-trim from tunables; no
        // pose-match. First-strike scans against the loco track;
        // chain-link scans against the outgoing one-shot (previous
        // attack mid-recovery). Wider chain-link window because the
        // best frame can be deeper into the new clip's windup.
        float pose_matched_start = -1.0f;
        if (!is_sprint_swap && first_strike)
        {
            pose_matched_start = poseMatchStartFromLoco(*ra.clip, 0.30f);
        }
        else if (!is_sprint_swap)
        {
            const auto fd_pre = sSampler.frameDiagnostics();
            if (fd_pre.one_shot_name != nullptr)
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
                            *prev_clip, fd_pre.one_shot_time, *ra.clip, joints, 0.0f, 0.50f);
                }
            }
        }
        const float fallback_start = is_sprint_swap ? tun.sprint_finisher_start_seconds
                                     : first_strike ? 0.0f
                                                    : ra.attack->resolved_chain_link_start_seconds;
        const float start_seconds =
            (pose_matched_start >= 0.0f) ? pose_matched_start : fallback_start;
        SpliceDiag diag;
        selva::anim::PoseSampler::FrameDiagnostics fd_pre_action{};
        if (selva::combat::isCombatDebugEnabled())
        {
            diag = captureSpliceDiag();
            fd_pre_action = sSampler.frameDiagnostics();
        }
        // Pick the profile for this fire. First-strike, chain-link,
        // and sprint-finisher have different blend/inertialization/
        // lockout choices baked into named profiles. Chain-link's
        // blend honours the per-attack override resolved into blend_in.
        TransitionProfile profile = is_sprint_swap ? profiles::sprintFinisher()
                                    : first_strike ? profiles::firstStrike()
                                                   : profiles::chainLink();
        if (!is_sprint_swap)
            profile.blend_in_seconds = blend_in;
        profile.mask = mask;
        const float rate = effectiveAttackPlaybackRate(hand);
        fireOneShotWithProfile(*ra.clip, profile, start_seconds, rate);

        if (selva::combat::isCombatDebugEnabled())
        {
            char prefix[256];
            std::snprintf(prefix, sizeof(prefix),
                          "[combat:action-splice] role=%s clip=%s blend=%.3fs start=%.3fs  "
                          "loco=%s clip_t=%.3fs ",
                          first_strike ? "first" : "chain", ra.attack->clip.c_str(), blend_in,
                          start_seconds, sLastLocoClipName.c_str(),
                          fd_pre_action.loco_current_time);
            logSpliceDiag(diag, *ra.clip, start_seconds, prefix);
        }

        // Arm-for-chain frame capture: kicks off on the FIRST swing of
        // a chain, runs for an estimated full-chain duration scaled by
        // playback rate. Outputs PNGs to build/bin/frame_capture/ and
        // a contact-sheet composite at end. Use to visually diagnose
        // chain transitions frame-by-frame (especially with playback
        // rate dropped to 0.3x for slow-mo).
        if (sFrameCaptureArmedNextChain && !sFrameCaptureActive && first_strike)
        {
            sFrameCaptureDir = "frame_capture";
            std::error_code ec;
            std::filesystem::create_directories(sFrameCaptureDir, ec);
            sFrameCaptureActive = true;
            sFrameCaptureCounter = 0;
            sFrameCaptureElapsed = 0.0f;
            // Estimate chain wall-clock length: sum of all entries in
            // this chain (full duration each, not just the cancel
            // window) divided by playback rate, plus a small tail for
            // recovery + locomotion blend.
            float chain_seconds = 0.0f;
            // Estimate uses the first technique of the matching slot.
            const auto* w = (hand == selva::combat::HandSide::Right) ? sEquipment.right
                                                                     : sEquipment.left;
            if (w != nullptr && w->cls != nullptr)
            {
                const auto& set = (sEquipment.grip == selva::combat::Grip::TwoHanded)
                                      ? w->cls->two_handed
                                      : w->cls->one_handed;
                const auto& techniques = (kind == AttackKind::Heavy)
                                             ? set.heavy
                                             : (kind == AttackKind::Running) ? set.running
                                                                             : set.light;
                if (!techniques.empty())
                {
                    for (const auto& a : techniques[0].attacks)
                    {
                        const auto* c = sClips.get(a.clip);
                        if (c != nullptr && c->isLoaded())
                            chain_seconds += c->duration();
                    }
                }
            }
            const float capture_rate = std::max(0.1f, rate);
            sFrameCaptureDuration = (chain_seconds / capture_rate) + 1.0f;
            sFrameCaptureArmedNextChain = false;
            std::fprintf(stderr,
                         "[frame-capture] capturing chain for %.2fs to %s/ (rate=%.2f)\n",
                         sFrameCaptureDuration, sFrameCaptureDir.c_str(), capture_rate);
        }
        if (selva::combat::isCombatDebugEnabled())
        {
            combatLog("[combat:fire] hand=%s idx=%d clip=%s dur=%.3fs "
                      "start=%.3fs (%.0f%%)  blend_in=%.3fs  rate=%.2f  role=%s\n",
                      hand == selva::combat::HandSide::Right ? "R" : "L", chain.chain_index,
                      ra.attack->clip.c_str(), ra.clip->duration(), start_seconds,
                      (start_seconds / std::max(0.001f, ra.clip->duration())) * 100.0f, blend_in,
                      rate, first_strike ? "first" : "chain-link");
            // Splice diagnostic: for chain-links, log the live right-
            // hand position (where the previous clip left it on screen
            // last frame) and the right-hand position at the new clip
            // sampled at start_seconds. The Euclidean distance between
            // these two is what inertialization has to bridge —
            // large gaps explain visible "jerk" feel and would mean
            // the splice math is wrong; small gaps mean the residual
            // is asset-side (clip bookend mismatch) and only Blender
            // re-pose can close it.
            if (!first_strike)
            {
                const int rh = sSampler.findJoint("mixamorig:RightHand");
                if (rh >= 0)
                {
                    const glm::vec3 live = sSampler.jointWorldPos(rh);
                    const glm::vec3 entry =
                        sSampler.sampleJointWorldPos(*ra.clip, start_seconds, rh);
                    const glm::vec3 decay_vec = entry - live;
                    const float decay_dist = glm::length(decay_vec);

                    // Diagnostic: where IS the previous one-shot (the
                    // outgoing slash) right now in its own timeline,
                    // and what does its pure pose look like there?
                    // Comparison of live vs prev-pure tells us how
                    // much the on-screen pose has drifted from the
                    // outgoing clip's deterministic pose due to
                    // multi-track blending (blend-out into locomotion,
                    // unfinished blend-in from idle, etc).
                    const auto fd = sSampler.frameDiagnostics();
                    const char* prev_clip_name =
                        fd.one_shot_name != nullptr ? fd.one_shot_name : "(none)";
                    glm::vec3 prev_pure(0.0f);
                    bool prev_pure_valid = false;
                    if (fd.one_shot_name != nullptr)
                    {
                        // Find the outgoing one-shot's clip via the
                        // registry name match. Sample it at one_shot_
                        // time to get its pure deterministic pose.
                        const auto* outgoing = sClips.get(fd.one_shot_name);
                        if (outgoing != nullptr && outgoing->isLoaded())
                        {
                            prev_pure =
                                sSampler.sampleJointWorldPos(*outgoing, fd.one_shot_time, rh);
                            prev_pure_valid = true;
                        }
                    }

                    // Pre-splice velocity: where the live hand was
                    // heading on this frame (this frame minus last
                    // frame's cached position). If we don't have a
                    // cached position yet (first time we ever log),
                    // mark NA.
                    glm::vec3 pre_vel(0.0f);
                    bool have_pre = false;
                    if (sLastRHValid)
                    {
                        pre_vel = live - sLastRHWorldPos;
                        have_pre = true;
                    }

                    // Post-splice velocity: where the new clip will
                    // move the hand once it starts playing forward.
                    // Sample at start and start + one frame (1/60s);
                    // forward direction = (next_pos - entry).
                    const float dt_frame = 1.0f / 60.0f;
                    const float t_next =
                        std::min(start_seconds + dt_frame, ra.clip->duration());
                    const glm::vec3 entry_next =
                        sSampler.sampleJointWorldPos(*ra.clip, t_next, rh);
                    const glm::vec3 post_vel = entry_next - entry;

                    auto angle_deg = [](const glm::vec3& a, const glm::vec3& b) -> float
                    {
                        const float la = glm::length(a);
                        const float lb = glm::length(b);
                        if (la < 1e-5f || lb < 1e-5f)
                            return 0.0f;
                        const float c = glm::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f);
                        return glm::degrees(std::acos(c));
                    };

                    const float pre_post_angle = have_pre ? angle_deg(pre_vel, post_vel) : -1.0f;
                    const float decay_post_angle = angle_deg(decay_vec, post_vel);

                    combatLog("  [splice] RH live=(%.3f,%.3f,%.3f)  "
                              "entry=(%.3f,%.3f,%.3f)  decay_dist=%.3fm\n",
                              live.x, live.y, live.z, entry.x, entry.y, entry.z, decay_dist);
                    if (have_pre)
                        combatLog("           pre_vel=(%+.3f,%+.3f,%+.3f) |%.3f|  "
                                  "post_vel=(%+.3f,%+.3f,%+.3f) |%.3f|  "
                                  "pre-post=%.0f deg\n",
                                  pre_vel.x, pre_vel.y, pre_vel.z, glm::length(pre_vel), post_vel.x,
                                  post_vel.y, post_vel.z, glm::length(post_vel), pre_post_angle);
                    else
                        combatLog("           pre_vel=NA (no cache)  "
                                  "post_vel=(%+.3f,%+.3f,%+.3f) |%.3f|\n",
                                  post_vel.x, post_vel.y, post_vel.z, glm::length(post_vel));
                    combatLog("           decay_vec=(%+.3f,%+.3f,%+.3f)  "
                              "decay-post_vel=%.0f deg  (~0=parallel-smear=hidden, "
                              "~90=orthogonal=visible jerk)\n",
                              decay_vec.x, decay_vec.y, decay_vec.z, decay_post_angle);
                    // Drift diagnostic: prev clip's pure pose at its
                    // current playing time vs. the live (multi-track-
                    // blended) pose. If these match, live IS slash's
                    // pure pose and inertialization is correctly
                    // bridging between two pure clips. If they differ,
                    // the on-screen pose has drifted toward neutral
                    // due to slash's blend-out / locomotion bleed /
                    // inertialization-from-idle still decaying.
                    if (prev_pure_valid)
                    {
                        const glm::vec3 drift = live - prev_pure;
                        combatLog("           prev=%s @ %.3fs/%.3fs  weight=%.2f  phase=%d  "
                                  "pure_RH=(%.3f,%.3f,%.3f)  drift=(%+.3f,%+.3f,%+.3f) |%.3fm|\n",
                                  prev_clip_name, fd.one_shot_time, fd.one_shot_duration,
                                  fd.one_shot_weight, fd.one_shot_phase, prev_pure.x, prev_pure.y,
                                  prev_pure.z, drift.x, drift.y, drift.z, glm::length(drift));
                    }
                    else
                    {
                        combatLog("           prev=%s (no clip lookup or inactive)\n",
                                  prev_clip_name);
                    }
                }
            }
        }
        // Anchor combat-stance to end-of-clip + grace so the stance
        // visibly persists through the attack's recovery instead of
        // dropping to peaceful while the player is still standing in
        // post-attack pose. Without this, the finisher's clip ends
        // ~2s after the press and combat_idle_grace_seconds elapses
        // at the same moment — stance drops immediately after the
        // attack rather than holding for the full grace window.
        // Effective clip duration shrinks with playback rate AND with
        // any chain-link start offset (we skip past the windup); the
        // stance anchor must follow it so grace doesn't stretch past
        // the visibly faster (and possibly trimmed) swing.
        const float effective_duration =
            std::max(0.0f, ra.clip->duration() - start_seconds) / rate;
        const float new_until =
            selva::wallClock() + effective_duration + tun.combat_idle_grace_seconds;
        if (new_until > sLocomotionSM.stance_active_until)
            sLocomotionSM.stance_active_until = new_until;
        // Cancel-window gate. Open time is auto-detected per attack at
        // load (clip_jointMotionEnd: when the weapon-hand has settled
        // back to combat-stance) and stored in resolved_cancel_open_
        // seconds. Once open, the window stays open — every press
        // advances the chain — until chain_reset_at elapses or the
        // chain ends (finisher rule). The open time scales with
        // attack_playback_rate so a faster swing has a proportionally
        // earlier cancel. When firing as a chain link the clip starts
        // at start_seconds (>0), so the wall-clock open offset is
        // (open - start) / rate — both numbers in clip-local time.
        const float clip_dur = ra.clip->duration();
        const float open_clip_local =
            std::clamp(ra.attack->resolved_cancel_open_seconds, 0.0f, clip_dur);
        const float remaining_clip = std::max(0.0f, clip_dur - start_seconds);
        const float remaining_to_open = std::max(0.0f, open_clip_local - start_seconds);
        const float open_wall = remaining_to_open / rate;
        chain.cancel_window_open_at = selva::wallClock() + open_wall;
        chain.cancel_window_close_at =
            chain.cancel_window_open_at + tun.combo_input_buffer_seconds;
        // chain_reset_at gates both chain auto-reset (so a slow
        // mid-chain press still advances) AND finisher-commit duration
        // (no new attacks until reset). Non-finisher steps need the
        // long window for chain-advance; finisher steps want the
        // short one (snappy combo-end recovery, matches lockout).
        const int next_idx = chain.chain_index + 1;
        const bool was_last = (next_idx >= ra.chain_size);
        if (was_last)
        {
            chain.chain_reset_at =
                chain.cancel_window_close_at + tun.attack_lockout_extension_seconds;
        }
        else
        {
            chain.chain_reset_at =
                selva::wallClock() + (remaining_clip / rate) + tun.combo_reset_grace_seconds;
        }
        chain.chain_kind = kind;
        // Finishers (the chain's last step) drop the locomotion
        // lockout so it ends when the one-shot fades out, instead of
        // extending to cancel_window_close_at + extension. The
        // extension exists to hold combat-stance during the input-
        // buffer window for chain advance — a finisher has nothing
        // to advance into, so the player can resume running the
        // moment the swing recovers. Sprint-finisher keeps its
        // wall-clock lockout (it's a self-contained running attack
        // with its own commit period).
        if (was_last && profile.lockout == TransitionProfile::Lockout::CancelWindowClose)
            profile.lockout = TransitionProfile::Lockout::None;
        applyProfileLockout(profile, chain.cancel_window_close_at);
        chain.chain_index = was_last ? 0 : next_idx;
        chain.is_finisher = was_last;
        return true;
    };

    // Try to fire the per-hand input — either a fresh press this
    // frame, or a buffered press whose cancel window just opened. If
    // neither is ready, return without firing. Returns the press to
    // apply this frame OR records a buffered press.
    auto tryAttackInput = [&](selva::combat::HandSide hand, bool press_edge_this_frame,
                               const char* button)
    {
        AttackChainState& chain =
            (hand == selva::combat::HandSide::Right) ? sChainRight : sChainLeft;
        BufferedPress& buf =
            (hand == selva::combat::HandSide::Right) ? sBufferedRight : sBufferedLeft;
        // Rhythm window: hit = inside [open, close]; before/after = miss.
        const bool first_strike = (chain.cancel_window_open_at <= 0.0f);
        const bool before_open =
            !first_strike && selva::wallClock() < chain.cancel_window_open_at;
        const bool past_close =
            !first_strike && selva::wallClock() > chain.cancel_window_close_at;
        // Finisher: cancel-fire is blocked during the swing's pre-contact
        // phase (before_open) so the player can't interrupt their own
        // finisher. From cancel_window_open_at onward the press starts
        // a NEW combo as a fresh first-strike — same rhythm as any
        // chain link, just one that resets to chain_index=0 instead of
        // advancing. Without this, presses past the finisher's contact
        // were silently dropped until chain_reset_at, producing a
        // ~0.6s blackout where "throw another combo" felt eaten.
        const bool finisher_restart =
            chain.is_finisher && !before_open && press_edge_this_frame;
        const bool in_window =
            !chain.is_finisher && (first_strike || (!before_open && !past_close));
        // Sprinting no longer routes to the Running technique slot at
        // press time — the Running clip (e.g. flying_knee_punch_combo)
        // is a CHAIN FINISHER, not a standalone first-strike. The
        // finisher swap happens inside fireAttack when chain_index
        // wraps to the final step.
        const AttackKind kind = shift_held ? AttackKind::Heavy : AttackKind::Light;

        // Fresh press this frame.
        if (press_edge_this_frame)
        {
            // Press during dodge: buffer for post-dodge fire instead
            // of either dropping or firing on top of the rolling
            // pose. The dodge commits; once it ends, the buffered
            // press fires as a normal first-strike from the settled
            // combat-idle pose. Lets the player chain rolls into
            // attacks without timing the press to the dodge's end
            // frame.
            if (sDodgeActive)
            {
                sPostDodgeAttack.pending = true;
                sPostDodgeAttack.hand = hand;
                sPostDodgeAttack.button = button;
                sPostDodgeAttack.buffered_at = selva::wallClock();
                combatLog("[combat:rhythm %.4fs] press BUFFERED (dodge active, "
                          "elapsed=%.3fs/%.3fs)\n",
                          selva::wallClock(), sDodgeElapsed, sDodgeDuration);
                return;
            }
            // First-press latch: delays fire so the locomotion track
            // can crossfade into the combat-stance idle BEFORE the
            // action plays. Two cases trigger it:
            //   (a) Stance is Peaceful — locomotion is on a peaceful
            //       idle/walk/run clip; we need the combat-idle beat
            //       so the action's t=0 leg pose matches what's on
            //       screen.
            //   (b) Stance is CombatReady but locomotion is on a
            //       MOVING clip (walking / running / a walk-transition).
            //       Without the establishing beat, the action splices
            //       directly from mid-walk-stride pose to the action's
            //       feet-together t=0 → leg spasm. The beat (combined
            //       with the SM is_moving override below) crossfades
            //       the loco track to unarmed_combat_idle / sword_idle
            //       so the action fires from a still combat pose.
            const bool need_establish_beat = sLocomotionSM.combat_stance == CombatStance::Peaceful ||
                                              isMovingLocoClip(sLastLocoClipName);
            if (need_establish_beat && !sPendingFirstAction.active)
            {
                // Validate the button against slot 0 of any technique.
                // Wrong-button first-strikes (e.g. RMB while unarmed
                // techniques all start with LMB) silently drop instead
                // of latching a swing the player can't trigger.
                const TechniqueDispatch td = dispatchTechniqueForPress(
                    sEquipment, hand, kind, /*chain_index=*/0,
                    /*current_locked=*/-1, button);
                if (!td.valid)
                {
                    combatLog("[combat:rhythm] first-strike REJECTED (no technique accepts %s @ slot 0)\n",
                              button);
                    return;
                }
                sPendingFirstAction.active = true;
                sPendingFirstAction.kind = PendingFirstActionKind::Attack;
                sPendingFirstAction.hand = hand;
                sPendingFirstAction.attack_kind = kind;
                sPendingFirstAction.button = button;
                sPendingFirstAction.fire_at =
                    selva::wallClock() + tun.combat_entry_delay_seconds;
                combat_input_this_frame = true;
                return;
            }
            if (sPendingFirstAction.active)
                return; // a first-action is already committed; ignore

            // Sprint bypass: while sprinting, ANY light press fires the
            // running attack regardless of where in the chain we are or
            // what button the locked technique expects next. Skips the
            // technique-dispatch validation entirely — fireAttack's
            // sprint-swap path replaces the resolved attack with the
            // running technique's clip and treats it as a finisher.
            // Reset chain state so the swap fires from a clean slate.
            // Drop the press if a sprint-finisher is already playing —
            // restarting would cancel the in-flight running attack mid
            // motion. The finisher commits.
            if (sPlayer.sprinting && kind == AttackKind::Light)
            {
                if (chain.is_finisher && sSampler.isOneShotActive())
                {
                    combatLog("[combat:rhythm] sprint press dropped (finisher in flight)\n");
                    return;
                }
                chain.chain_index = 0;
                chain.technique_index = -1;
                chain.is_finisher = false;
                if (fireAttack(hand, kind))
                    combat_input_this_frame = true;
                buf.pending = false;
                return;
            }

            // Press past a finisher's cancel_window_open_at: start a
            // fresh combo, don't drop the input. Same dispatch as
            // first_strike below, but with explicit chain reset since
            // we're coming out of finisher state.
            if (finisher_restart)
            {
                const TechniqueDispatch td = dispatchTechniqueForPress(
                    sEquipment, hand, kind, /*chain_index=*/0,
                    /*current_locked=*/-1, button);
                if (!td.valid)
                {
                    combatLog("[combat:rhythm] finisher-restart REJECTED (no technique accepts %s @ slot 0)\n",
                              button);
                    buf.pending = false;
                    return;
                }
                chain.chain_index = 0;
                chain.technique_index = td.locked;
                chain.is_finisher = false;
                combatLog("[combat:rhythm] finisher-restart -> fresh first-strike %s\n", button);
                if (fireAttack(hand, kind))
                    combat_input_this_frame = true;
                buf.pending = false;
                return;
            }

            if (first_strike)
            {
                // First press: dispatch against slot 0 to filter
                // techniques. If no technique accepts this button at
                // slot 0, fall through silently — no swing fires.
                const TechniqueDispatch td = dispatchTechniqueForPress(
                    sEquipment, hand, kind, /*chain_index=*/0,
                    /*current_locked=*/-1, button);
                if (!td.valid)
                {
                    combatLog("[combat:rhythm] press REJECTED (no technique accepts %s @ slot 0)\n",
                              button);
                    buf.pending = false;
                    return;
                }
                chain.technique_index = td.locked;
                if (fireAttack(hand, kind))
                    combat_input_this_frame = true;
                buf.pending = false;
                return;
            }
            if (in_window)
            {
                // Locked or unlocked: validate the press button against
                // the (already-fired chain.chain_index) — that's the
                // step we're about to advance INTO. fireAttack reads
                // chain.chain_index; here we're past the previous fire,
                // so chain.chain_index already points at the next slot.
                const TechniqueDispatch td = dispatchTechniqueForPress(
                    sEquipment, hand, kind, chain.chain_index,
                    chain.technique_index, button);
                if (!td.valid)
                {
                    // Wrong button mid-chain. Don't eat the input —
                    // try to dispatch as a fresh first-strike (slot 0
                    // of any technique). If the button matches a slot-0
                    // entry, we restart the chain. Otherwise drop.
                    const TechniqueDispatch fresh = dispatchTechniqueForPress(
                        sEquipment, hand, kind, /*chain_index=*/0,
                        /*current_locked=*/-1, button);
                    if (!fresh.valid)
                    {
                        combatLog("[combat:rhythm] press REJECTED (button %s matches neither "
                                  "locked technique %d @ slot %d nor any slot-0)\n",
                                  button, chain.technique_index, chain.chain_index);
                        buf.pending = false;
                        return;
                    }
                    chain.chain_index = 0;
                    chain.technique_index = fresh.locked;
                    chain.is_finisher = false;
                    combatLog("[combat:rhythm] mid-chain wrong-button -> restart as fresh first-strike %s\n",
                              button);
                    if (fireAttack(hand, kind))
                        combat_input_this_frame = true;
                    buf.pending = false;
                    return;
                }
                chain.technique_index = td.locked;
                const float center = 0.5f * (chain.cancel_window_open_at +
                                             chain.cancel_window_close_at);
                const float half_width = 0.5f * (chain.cancel_window_close_at -
                                                 chain.cancel_window_open_at);
                const float d = std::fabs(selva::wallClock() - center);
                chain.last_press_accuracy =
                    (half_width > 0.0f) ? std::max(0.0f, 1.0f - (d / half_width)) : 0.0f;
                chain.last_press_was_perfect =
                    chain.last_press_accuracy >= tun.perfect_accuracy_threshold;
                combatLog("[combat:rhythm] HIT accuracy=%.3f%s\n",
                          chain.last_press_accuracy,
                          chain.last_press_was_perfect ? " PERFECT" : "");
                if (fireAttack(hand, kind))
                    combat_input_this_frame = true;
                buf.pending = false;
                return;
            }
            if (before_open)
            {
                // Early press during the previous swing's pre-window
                // phase. Don't drop — buffer it. When the cancel window
                // opens this frame or a later frame, the buffered press
                // replays through dispatch and advances the chain. This
                // is the rhythm-window early-press grace: a player who
                // presses too eagerly gets their input honored on the
                // beat instead of feeling like the game ate the click.
                buf.pending = true;
                buf.kind = kind;
                buf.button = button;
                buf.buffered_at = selva::wallClock();
                combatLog("[combat:rhythm] BUFFERED (early; %s @ slot %d)\n", button,
                          chain.chain_index);
                return;
            }
            if (past_close)
            {
                // Late press: rhythm window closed. Don't eat the
                // input — treat as fresh first-strike if the button
                // is valid at slot 0. The "missed" state used to
                // lock subsequent presses too; instead, just
                // restart the chain.
                const TechniqueDispatch fresh = dispatchTechniqueForPress(
                    sEquipment, hand, kind, /*chain_index=*/0,
                    /*current_locked=*/-1, button);
                if (!fresh.valid)
                {
                    combatLog("[combat:rhythm] late press REJECTED (no technique accepts %s @ slot 0)\n",
                              button);
                    buf.pending = false;
                    return;
                }
                chain.chain_index = 0;
                chain.technique_index = fresh.locked;
                chain.is_finisher = false;
                combatLog("[combat:rhythm] late press -> restart as fresh first-strike %s\n", button);
                if (fireAttack(hand, kind))
                    combat_input_this_frame = true;
                buf.pending = false;
                return;
            }
            // Reachable only for chain.is_finisher && before_open —
            // press during the finisher's pre-contact swing. Drop;
            // can't cancel the finisher mid-windup. Press past
            // cancel_window_open_at goes through finisher_restart.
            buf.pending = false;
            return;
        }

        // No fresh press; if a buffered press exists and the window is
        // open, replay it through dispatch so the technique-locking
        // logic sees it. Wrong-button buffered press restarts as
        // fresh first-strike (consistent with the live wrong-button
        // path above).
        if (buf.pending && in_window)
        {
            const TechniqueDispatch td = dispatchTechniqueForPress(
                sEquipment, hand, buf.kind, chain.chain_index,
                chain.technique_index, buf.button);
            if (td.valid)
            {
                chain.technique_index = td.locked;
                if (fireAttack(hand, buf.kind))
                    combat_input_this_frame = true;
            }
            else
            {
                const TechniqueDispatch fresh = dispatchTechniqueForPress(
                    sEquipment, hand, buf.kind, /*chain_index=*/0,
                    /*current_locked=*/-1, buf.button);
                if (fresh.valid)
                {
                    chain.chain_index = 0;
                    chain.technique_index = fresh.locked;
                    chain.is_finisher = false;
                    if (fireAttack(hand, buf.kind))
                        combat_input_this_frame = true;
                }
            }
            buf.pending = false;
        }
    };

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
                    sPendingFirstAction.kind = PendingFirstActionKind::Block;
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

        // First-action latch: fires whichever action was committed
        // from Peaceful (attack or block) once the entry delay elapses.
        if (sPendingFirstAction.active && selva::wallClock() >= sPendingFirstAction.fire_at)
        {
            if (sPendingFirstAction.kind == PendingFirstActionKind::Attack)
            {
                AttackChainState& chain =
                    (sPendingFirstAction.hand == selva::combat::HandSide::Right) ? sChainRight
                                                                                  : sChainLeft;
                // Lock the technique using the latched button so dispatch
                // sees the same input a live press would have.
                const TechniqueDispatch td = dispatchTechniqueForPress(
                    sEquipment, sPendingFirstAction.hand, sPendingFirstAction.attack_kind,
                    /*chain_index=*/0, /*current_locked=*/-1, sPendingFirstAction.button);
                if (td.valid)
                {
                    chain.technique_index = td.locked;
                    if (fireAttack(sPendingFirstAction.hand, sPendingFirstAction.attack_kind))
                        combat_input_this_frame = true;
                }
            }
            else
            {
                fireBlock(true, sPendingFirstAction.block_clip,
                          sPendingFirstAction.block_freeze_last);
                combat_input_this_frame = true;
            }
            sPendingFirstAction.active = false;
        }
    }
    sPrevLMB = lmbNow;
    sPrevRMB = rmbNow;
    sPrevF = (keys[SDL_SCANCODE_F] != 0);

    // Cache this frame's right-hand position for next frame's splice
    // diagnostic — used to compute pre-splice velocity (live - cached).
    {
        const int rh = sSampler.findJoint("mixamorig:RightHand");
        if (rh >= 0)
        {
            sLastRHWorldPos = sSampler.jointWorldPos(rh);
            sLastRHValid = true;
        }
    }

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
    const glm::vec3 camFwd(-std::sin(sCamYaw), 0.0f, -std::cos(sCamYaw));
    const glm::vec3 camRight(std::cos(sCamYaw), 0.0f, -std::sin(sCamYaw));
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
        AttackChainState& chain = (sPostDodgeAttack.hand == selva::combat::HandSide::Right)
                                      ? sChainRight
                                      : sChainLeft;
        chain.chain_index = 0;
        chain.technique_index = -1;
        chain.is_finisher = false;
        const TechniqueDispatch td = dispatchTechniqueForPress(
            sEquipment, sPostDodgeAttack.hand, AttackKind::Light, /*chain_index=*/0,
            /*current_locked=*/-1, sPostDodgeAttack.button);
        if (td.valid)
        {
            chain.technique_index = td.locked;
            if (fireAttack(sPostDodgeAttack.hand, AttackKind::Light))
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
        const bool in_attack_recovery = selva::wallClock() < sLocoLockoutUntil;
        const bool loco_lockout = attack_in_flight || in_attack_recovery;
        const bool is_moving = wasd_intent && !loco_lockout;
        const bool is_sprinting = sPlayer.sprinting && !loco_lockout;
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
                          in_attack_recovery ? 1 : 0, is_moving ? 1 : 0, sLocoLockoutUntil,
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
        clip = sClips.get(clip_name);
        const bool loco_clip_changed = (clip_name != sLastLocoClipName);
        // Trace SM clip-choice changes. Pairs with the input trace so
        // we can see exactly which press caused which transition.
        // Includes loco_current_time so we know what frame of the
        // outgoing clip is the splice source — combined with the
        // dest clip's t=0, that's enough to compute pose mismatch.
        if (selva::combat::isCombatDebugEnabled() && loco_clip_changed)
        {
            const auto fd = sSampler.frameDiagnostics();
            combatLog("[sm %.4fs] loco-pick %s@%.3fs -> %s (blend=%.3fs)\n", selva::wallClock(),
                      sLastLocoClipName.c_str(), fd.loco_current_time, clip_name.c_str(),
                      sm_out.blend_seconds);
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

// ---------------------------------------------------------------------------
// Render — issue one draw call per object. View-projection is set once per
// frame; uModel and uTint vary per draw.
// ---------------------------------------------------------------------------

static void selvaRenderWorld(Engine& /*engine*/, EntityManager& /*em*/, float /*camX*/,
                             float /*camY*/, float /*alpha*/)
{
    ZoneScopedN("selvaRenderWorld");
    // Camera is positioned behind the player along its forward axis, lifted
    // by kFollowHeight, looking at the player's chest.
    const glm::vec3 lookFwd(std::cos(sCamPitch) * -std::sin(sCamYaw), std::sin(sCamPitch),
                            std::cos(sCamPitch) * -std::cos(sCamYaw));
    const auto& tun = selva::tuning::current();
    const glm::vec3 camPos =
        sPlayer.pos - lookFwd * tun.follow_distance + glm::vec3(0.0f, tun.follow_height, 0.0f);
    // LookAt height tracks the character's chest (~1.3m) since the
    // rigged character is ~1.7m tall. Aiming at chest height keeps
    // the player's silhouette centered in the frame instead of
    // head-up or feet-down.
    // Camera lookAt height tracks the character's hip Y so that dynamic
    // poses (rolls, knockdowns, jumps) keep the body in frame. The
    // legacy "1.3m chest height" was a hardcoded constant tuned for a
    // standing character; during a tucked tumble the actual chest
    // drops to ~0.85m and the character literally fell out of the
    // camera's aim, looking like it disappeared. Hip-tracking fixes
    // that without changing standing-pose framing.
    //
    // BUT: tracking hip Y instantaneously means the camera dives with
    // the roll's ~0.8m vertical excursion in ~0.4s, which reads as
    // jarring camera shake. We decouple camera vertical settle from
    // character vertical motion: the camera lags behind fast Y
    // changes, so a roll looks like the character ducks beneath a
    // steady frame instead of the world tilting around them.
    //
    // Implementation: exponential decay toward the target hip-Y with
    // a time constant of ~0.4s. Per-frame this is `lerp(current,
    // target, 1 - exp(-dt/tau))`. We use frame_dt (wall clock) since
    // this runs in the render path.
    float targetLookAtY = 1.3f;
    if (sSampler.jointCount() > 0)
    {
        for (int i = 0; i < sSampler.jointCount(); ++i)
        {
            if (std::strcmp(sSampler.jointName(i), "mixamorig:Hips") == 0)
            {
                targetLookAtY = sSampler.jointWorldPos(i).y + 0.3f; // a bit above hip
                break;
            }
        }
    }
    static float sSmoothedLookAtY = targetLookAtY;
    static bool sLookAtYInit = false;
    if (!sLookAtYInit)
    {
        sSmoothedLookAtY = targetLookAtY; // first-frame snap, no easing
        sLookAtYInit = true;
    }
    else
    {
        constexpr float kLookAtTau = 0.4f; // seconds; higher = lazier camera
        // Render runs at wall-clock rate (not fixed-step), so derive dt
        // from SDL ticks rather than threading the engine dt down here.
        static Uint64 sPrevTicks = SDL_GetTicks64();
        const Uint64 nowTicks = SDL_GetTicks64();
        const float frame_dt = static_cast<float>(nowTicks - sPrevTicks) * 0.001f;
        sPrevTicks = nowTicks;
        const float alpha = 1.0f - std::exp(-frame_dt / kLookAtTau);
        sSmoothedLookAtY += (targetLookAtY - sSmoothedLookAtY) * alpha;
    }
    const glm::vec3 lookAt = glm::vec3(sPlayer.pos.x, sSmoothedLookAtY, sPlayer.pos.z);
    const glm::mat4 view = glm::lookAt(camPos, lookAt, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        sWindowH > 0 ? static_cast<float>(sWindowW) / static_cast<float>(sWindowH) : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(tun.fov_degrees), aspect, 0.1f, 200.0f);
    const glm::mat4 viewProj = proj * view;

    selva::render::useSceneProgram();
    selva::render::setSceneViewProj(viewProj);

    // 1. Floor — unrotated, identity model. Rendered first; depth test
    //    handles ordering against everything else.
    selva::render::drawFloor(glm::mat4(1.0f), 1.0f);

    // 1b. 1m grid + cardinal axes — readable ruler for movement debugging.
    selva::render::drawGrid(glm::mat4(1.0f), 1.0f);
    selva::render::drawAxes(glm::mat4(1.0f), 1.0f);

    // 2. Scene cube — tumbles at the origin, lifted so it doesn't half-bury.
    {
        const float seconds = static_cast<float>(SDL_GetTicks64()) * 0.001f;
        const float angle = seconds * (glm::two_pi<float>() / 4.0f);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.5f, -8.0f));
        model = glm::rotate(model, angle, glm::normalize(glm::vec3(0.6f, 1.0f, 0.3f)));
        selva::render::drawCube(model, 0.7f);
    }

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
        const int src_w = sWindowW;
        const int src_h = sWindowH;
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

// ---------------------------------------------------------------------------
// Snapped slider — ImGui::SliderFloat with post-hoc rounding to a step. The
// step makes drag-tuning land on round values (0.5, 1.0, 5.0) instead of
// 0.4783 — easier to settle on a value, easier to reason about. Ctrl+click
// still allows fine-grained typed entry; this only affects drag.
//
// fmt should match step's precision (e.g. "%.2f" for step=0.05).
static void tunedSlider(const char* label, float* val, float min, float max, float step,
                        const char* fmt = "%.2f")
{
    if (ImGui::SliderFloat(label, val, min, max, fmt))
    {
        if (step > 0.0f)
            *val = std::round(*val / step) * step;
    }
}

// ---------------------------------------------------------------------------
// In-game tuning panel — draws an ImGui window with sliders for every
// tunable. Live values; edits take effect on the next frame. Adding a new
// tunable: a one-line tunedSlider here and a field on Tunables.
// ---------------------------------------------------------------------------

// What button the next chain press needs. Reads the locked technique
// when one is set; otherwise scans every technique's slot for distinct
// buttons. Returns "L", "R", or "L/R" for the unlocked-ambiguous case.
// "—" when no technique exposes this slot.
static const char* nextExpectedButtonLabel(const AttackChainState& chain, AttackKind kind)
{
    using selva::combat::Grip;
    using selva::combat::HandSide;
    const selva::combat::Weapon* w = sEquipment.right;
    if (w == nullptr || w->cls == nullptr)
        return "-";
    const auto& aset = (sEquipment.grip == Grip::TwoHanded) ? w->cls->two_handed : w->cls->one_handed;
    const auto* techniques = &aset.light;
    if (kind == AttackKind::Heavy)
        techniques = &aset.heavy;
    else if (kind == AttackKind::Running && !aset.running.empty())
        techniques = &aset.running;
    if (techniques->empty())
        return "-";

    auto label_for = [](const std::string& s) -> const char*
    {
        if (s == "LMB")
            return "L";
        if (s == "RMB")
            return "R";
        return "*"; // any/empty — accepts either
    };

    if (chain.technique_index >= 0 &&
        chain.technique_index < static_cast<int>(techniques->size()))
    {
        const auto& atks = (*techniques)[chain.technique_index].attacks;
        if (chain.chain_index < 0 || chain.chain_index >= static_cast<int>(atks.size()))
            return "-";
        return label_for(atks[chain.chain_index].expected_button);
    }
    // Unlocked: union of every technique's slot at chain_index.
    bool has_lmb = false;
    bool has_rmb = false;
    bool has_any = false;
    for (const auto& tech : *techniques)
    {
        if (chain.chain_index < 0 || chain.chain_index >= static_cast<int>(tech.attacks.size()))
            continue;
        const auto& exp = tech.attacks[chain.chain_index].expected_button;
        if (exp == "LMB")
            has_lmb = true;
        else if (exp == "RMB")
            has_rmb = true;
        else
            has_any = true;
    }
    if (has_any || (has_lmb && has_rmb))
        return "L/R";
    if (has_lmb)
        return "L";
    if (has_rmb)
        return "R";
    return "-";
}

// Combo HUD: always-on overlay showing current chain step, the next-
// expected button, the rhythm window (with the perfect sub-zone shown
// in a brighter color), and last-press accuracy.
static void renderComboHud()
{
    const auto& chain = sChainRight; // unarmed shares the right chain
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 360.0f;
    const float h = 80.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y - h - 30.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##ComboHUD", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoMove);

    // Step counter + last press result.
    const char* state = chain.last_press_was_perfect       ? "PERFECT"
                        : (chain.last_press_accuracy > 0.0f) ? "HIT"
                                                             : "READY";
    ImGui::Text("Combo: %d  -  %s  acc=%.2f", chain.chain_index, state,
                chain.last_press_accuracy);

    // Next-button label drawn left of the rhythm bar. "L" = LMB, "R" =
    // RMB, "L/R" = either accepts (only in the unlocked-ambiguous
    // window between press 1 and the press that locks the technique).
    const char* next_btn = nextExpectedButtonLabel(chain, AttackKind::Light);
    const float btn_box_w = 28.0f;
    const float bar_h = 16.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 btn_bg = IM_COL32(60, 60, 100, 220);
    const ImU32 btn_fg = IM_COL32(230, 230, 255, 255);
    draw->AddRectFilled(pos, ImVec2(pos.x + btn_box_w, pos.y + bar_h), btn_bg);
    const ImVec2 ts = ImGui::CalcTextSize(next_btn);
    draw->AddText(ImVec2(pos.x + (btn_box_w - ts.x) * 0.5f,
                         pos.y + (bar_h - ts.y) * 0.5f),
                  btn_fg, next_btn);

    // Window bar: horizontal, filled green inside [open, close], with a
    // brighter inner band for the "perfect" sub-zone, and a marker for
    // current wall-clock time.
    const float open = chain.cancel_window_open_at;
    const float close = chain.cancel_window_close_at;
    const float now = selva::wallClock();
    const float view_secs = 1.0f; // window of time around `now`
    const float bar_x = pos.x + btn_box_w + 6.0f;
    const float bar_w = w - 16.0f - (btn_box_w + 6.0f);
    const ImU32 bg = IM_COL32(40, 40, 40, 200);
    const ImU32 win = IM_COL32(60, 200, 80, 220);
    const ImU32 perfect = IM_COL32(255, 220, 80, 240);
    const ImU32 marker = IM_COL32(255, 240, 80, 255);
    draw->AddRectFilled(ImVec2(bar_x, pos.y), ImVec2(bar_x + bar_w, pos.y + bar_h), bg);
    if (close > open && open > 0.0f)
    {
        const float view_start = now - view_secs * 0.5f;
        const float view_end = now + view_secs * 0.5f;
        auto t_to_x = [&](float t) -> float
        {
            const float u = (t - view_start) / (view_end - view_start);
            return bar_x + std::clamp(u, 0.0f, 1.0f) * bar_w;
        };
        const float x_open = t_to_x(open);
        const float x_close = t_to_x(close);
        if (x_close > x_open)
            draw->AddRectFilled(ImVec2(x_open, pos.y), ImVec2(x_close, pos.y + bar_h), win);

        // Perfect band: the central fraction of the window where
        // last_press_accuracy >= perfect_accuracy_threshold. accuracy
        // = 1 - |dt| / half_width; perfect needs accuracy >= T, i.e.
        // |dt| <= half_width * (1 - T). Width on screen = full window
        // width * (1 - T).
        const float threshold = selva::tuning::current().perfect_accuracy_threshold;
        const float perfect_frac = std::clamp(1.0f - threshold, 0.0f, 1.0f);
        const float center = 0.5f * (open + close);
        const float half = 0.5f * (close - open) * perfect_frac;
        const float x_perfect_open = t_to_x(center - half);
        const float x_perfect_close = t_to_x(center + half);
        if (x_perfect_close > x_perfect_open)
            draw->AddRectFilled(ImVec2(x_perfect_open, pos.y),
                                ImVec2(x_perfect_close, pos.y + bar_h), perfect);
    }
    const float x_now = bar_x + bar_w * 0.5f;
    draw->AddLine(ImVec2(x_now, pos.y - 2), ImVec2(x_now, pos.y + bar_h + 2), marker, 2.0f);
    ImGui::Dummy(ImVec2(w - 16.0f, bar_h));

    ImGui::End();
}

static void selvaRenderImGui(Engine& /*engine*/, EntityManager& /*em*/)
{
    if (selva::combat::isCombatDebugEnabled())
        renderComboHud();

    if (!sShowTuningPanel)
        return;

    auto& tun = selva::tuning::current();
    ImGui::Begin("Selva Oscura Tuning (F1)", &sShowTuningPanel);

    if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Time scale", &tun.time_scale, 0.05f, 2.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("1x"))
            tun.time_scale = 1.0f;
        ImGui::SameLine();
        if (ImGui::SmallButton("0.25x"))
            tun.time_scale = 0.25f;
    }

    if (ImGui::CollapsingHeader("Locomotion", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Turn rate (rad/s)", &tun.turn_rate, 1.0f, 30.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Mouse-look", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Sensitivity", &tun.mouse_sensitivity, 0.0005f, 0.01f, 0.0005f, "%.4f");
        tunedSlider("Pitch min", &tun.pitch_min, -1.55f, 0.0f, 0.05f, "%.2f");
        tunedSlider("Pitch max", &tun.pitch_max, 0.0f, 1.55f, 0.05f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Follow distance", &tun.follow_distance, 1.0f, 15.0f, 0.5f, "%.1f");
        tunedSlider("Follow height", &tun.follow_height, 0.0f, 8.0f, 0.5f, "%.1f");
        tunedSlider("FOV (deg)", &tun.fov_degrees, 30.0f, 110.0f, 5.0f, "%.0f");
    }

    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Cross-fade (s)", &tun.anim_blend_seconds, 0.0f, 0.50f, 0.025f, "%.3f");
        tunedSlider("Combat-idle grace (s)", &tun.combat_idle_grace_seconds, 0.0f, 5.0f, 0.25f,
                    "%.2f");
        tunedSlider("Combat entry delay (s)", &tun.combat_entry_delay_seconds, 0.0f, 0.50f, 0.025f,
                    "%.3f");
    }

    if (ImGui::CollapsingHeader("Combat", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Live chain step indicator for diagnostics. chain_index is 0
        // when no chain has started or when a chain has ended (auto-
        // reset). chain_index increments on each successful fire and
        // wraps based on chain length. Right-hand and left-hand chains
        // tracked separately so dual-wield diagnostics work later.
        ImGui::Text("Chain  R: idx=%d%s%s   L: idx=%d%s%s", sChainRight.chain_index,
                    sChainRight.is_finisher ? " FIN" : "",
                    sChainRight.cancel_window_open_at > selva::wallClock() ? " (pre-window)"
                                                                          : "",
                    sChainLeft.chain_index, sChainLeft.is_finisher ? " FIN" : "",
                    sChainLeft.cancel_window_open_at > selva::wallClock() ? " (pre-window)" : "");
        tunedSlider("Combo reset grace (s)", &tun.combo_reset_grace_seconds, 0.05f, 2.0f, 0.05f,
                    "%.2f");
        tunedSlider("Input buffer (s)", &tun.combo_input_buffer_seconds, 0.05f, 0.50f, 0.025f,
                    "%.3f");
        tunedSlider("First-strike blend (s)", &tun.first_strike_blend_seconds, 0.05f, 0.50f,
                    0.025f, "%.3f");
        tunedSlider("Chain blend (s)", &tun.combo_chain_blend_seconds, 0.05f, 0.50f, 0.025f,
                    "%.3f");
        tunedSlider("Attack playback rate", &tun.attack_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
        // Auto-re-resolves on change so the slider is live.
        if (ImGui::SliderFloat("Cancel velocity frac", &tun.cancel_open_velocity_fraction, 0.05f,
                               0.95f, "%.2f"))
            resolveAttackCancelOpenTimes();
        if (ImGui::Button("Re-resolve attack windows"))
            resolveAttackCancelOpenTimes();
        // Master debug switch — gates the combo HUD, the per-fire
        // combatLog spam, the splice-distance joint samples, and the
        // chain-link velocity diag. Off by default in normal play
        // because the per-attack diagnostic work is heavy enough to
        // visibly drop frames (Tracy showed selvaPerFrame max=77ms
        // with it on vs ~1ms baseline). Flip on when iterating.
        bool combat_debug_enabled = selva::combat::isCombatDebugEnabled();
        if (ImGui::Checkbox("Combat debug overlay + diagnostics", &combat_debug_enabled))
            selva::combat::setCombatDebugEnabled(combat_debug_enabled);

        // Live tuning slider for the equipped weapon's 1H light chain
        // entry [1] (the "second swing" — slash_3 in the default
        // sword loadout). Bypasses the auto-detect motion_start - 0.05
        // splice; the value here goes straight into resolved_chain_
        // link_start_seconds. Only useful while iterating on a
        // specific transition; once the right value is found, copy
        // it into the chain entry's `chain_link_start_seconds` field
        // in the weapon-class JSON. Re-resolve is invoked
        // automatically on slider change so the next chain fire
        // picks up the new value immediately.
        if (sEquipment.right != nullptr && sEquipment.right->cls != nullptr)
        {
            // Mutate via the registry's mutable map (sEquipment.right
            // holds const pointers — its weapon -> cls path is const).
            // Look up the class entry by id and edit there.
            auto it = sWeaponClasses.by_id.find(sEquipment.right->cls->id);
            if (it != sWeaponClasses.by_id.end() && !it->second.one_handed.light.empty())
            {
                auto& chain_vec = it->second.one_handed.light[0].attacks;
                if (chain_vec.size() >= 2)
                {
                    auto& slot1 = chain_vec[1];
                    const auto* clip = sClips.get(slot1.clip);
                    const float dur =
                        (clip != nullptr && clip->isLoaded()) ? clip->duration() : 1.5f;
                    float val = slot1.resolved_chain_link_start_seconds;
                    if (ImGui::SliderFloat("Slot1 chain_link_start (s)", &val, 0.0f, dur, "%.3f"))
                    {
                        slot1.chain_link_start_seconds = val;
                        slot1.resolved_chain_link_start_seconds = val;
                    }
                    // Per-attack blend duration. Live-edit; the next
                    // chain fire reads chain_link_blend_seconds. Range
                    // 0.05–0.80s — too short snaps, too long is
                    // sluggish.
                    float blend_val = (slot1.chain_link_blend_seconds >= 0.0f)
                                          ? slot1.chain_link_blend_seconds
                                          : tun.combo_chain_blend_seconds;
                    if (ImGui::SliderFloat("Slot1 chain_link_blend (s)", &blend_val, 0.05f, 0.80f,
                                           "%.3f"))
                        slot1.chain_link_blend_seconds = blend_val;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Dodge", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Roll playback rate", &tun.roll_playback_rate, 0.5f, 2.5f, 0.05f, "%.2f");
        tunedSlider("Backstep playback rate", &tun.backstep_playback_rate, 0.5f, 2.5f, 0.05f,
                    "%.2f");
        tunedSlider("Tap window (s)", &tun.dodge_tap_window, 0.05f, 0.40f, 0.025f, "%.3f");
        tunedSlider("Steer rate (rad/s)", &tun.dodge_steer_rate, 0.0f, 15.0f, 0.5f, "%.1f");
    }

    if (ImGui::CollapsingHeader("Sprint Finisher", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Start trim (s)", &tun.sprint_finisher_start_seconds, 0.0f, 0.50f, 0.025f,
                    "%.3f");
        tunedSlider("Blend-in (s)", &tun.sprint_finisher_blend_in_seconds, 0.0f, 0.40f, 0.025f,
                    "%.3f");
        tunedSlider("Lockout duration (s)", &tun.sprint_finisher_lockout_seconds, 0.10f, 3.0f,
                    0.05f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Post-Attack Lockout", ImGuiTreeNodeFlags_DefaultOpen))
    {
        tunedSlider("Extension past cancel window (s)", &tun.attack_lockout_extension_seconds,
                    0.0f, 1.50f, 0.025f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Animation Debug", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Build a sorted list of clip names from the registry. Static
        // cache so we're not allocating per frame; rebuilt only when the
        // registry size changes (it doesn't change after startup, but
        // be resilient).
        static std::vector<std::string> clip_names_sorted;
        if (clip_names_sorted.size() != sClips.by_name.size())
        {
            clip_names_sorted.clear();
            clip_names_sorted.reserve(sClips.by_name.size());
            for (const auto& kv : sClips.by_name)
                clip_names_sorted.push_back(kv.first);
            std::sort(clip_names_sorted.begin(), clip_names_sorted.end());
        }

        ImGui::TextUnformatted("Override: play one clip in isolation,");
        ImGui::TextUnformatted("bypass gameplay state machine.");

        const char* current_label =
            sDebugClipName.empty() ? "(off — gameplay drives anim)" : sDebugClipName.c_str();
        if (ImGui::BeginCombo("Clip", current_label))
        {
            // First entry: turn debug off.
            const bool none_selected = sDebugClipName.empty();
            if (ImGui::Selectable("(off — gameplay drives anim)", none_selected))
                sDebugClipName.clear();
            ImGui::Separator();
            for (const auto& name : clip_names_sorted)
            {
                const bool selected = (name == sDebugClipName);
                if (ImGui::Selectable(name.c_str(), selected))
                    sDebugClipName = name;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (!sDebugClipName.empty())
        {
            const auto* dbg = sClips.get(sDebugClipName);
            if (dbg != nullptr && dbg->isLoaded())
            {
                ImGui::Text("duration: %.3fs   tracks: %d", dbg->duration(), dbg->trackCount());
            }
            if (ImGui::Button("Stop debug clip"))
                sDebugClipName.clear();

            // CSV recorder. Captures every frame's joint world-space
            // positions while the named clip plays in isolation, writes
            // to debug_bones_game_<clip>.csv next to the exe. The
            // browser previewer has a matching "Export bone CSV" button
            // that produces the same shape; a python diff script
            // compares them.
            ImGui::SameLine();
            if (sDebugRecording)
            {
                ImGui::Text("Recording... %.2f / %.2fs", sDebugRecordElapsed, sDebugRecordDuration);
            }
            else if (dbg != nullptr && dbg->isLoaded() && ImGui::Button("Record CSV"))
            {
                sDebugRecording = true;
                sDebugRecordElapsed = 0.0f;
                sDebugRecordDuration = dbg->duration();
                sDebugRecordClipName = sDebugClipName;
                sDebugRecordSamples.clear();
                sDebugRecordSamples.reserve(static_cast<std::size_t>(dbg->duration() * 65.0f));
            }
        }

        // Dodge recorder: armed via this button, fires the next time
        // a dodge plays (Space tap). Lets us measure the playOneShot
        // path's bone trajectories — comparing against the F1 update()
        // recording of the same clip reveals whether the two paths
        // produce different poses for the same input clip.
        ImGui::Separator();
        if (sDebugRecordArmedNextDodge)
        {
            ImGui::TextUnformatted("Dodge recorder ARMED — tap Space to capture.");
            if (ImGui::Button("Cancel arm"))
                sDebugRecordArmedNextDodge = false;
        }
        else if (sDebugRecording && sDebugRecordClipName.rfind("dodge_", 0) == 0)
        {
            ImGui::Text("Recording dodge... %.2f / %.2fs", sDebugRecordElapsed,
                        sDebugRecordDuration);
        }
        else
        {
            if (ImGui::Button("Arm dodge recorder"))
                sDebugRecordArmedNextDodge = true;
        }

        // Frame capture: write a PNG per rendered frame for the next
        // dodge. PNGs land in build/bin/frame_capture/. Use to visually
        // diagnose what's happening on screen during the dodge instead
        // of relying on description.
        ImGui::Separator();
        if (sFrameCaptureArmedNextChain)
        {
            ImGui::TextUnformatted("Frame capture ARMED — click LMB to start chain.");
            if (ImGui::Button("Cancel chain frame arm"))
                sFrameCaptureArmedNextChain = false;
        }
        else if (sFrameCaptureArmedNextDodge)
        {
            ImGui::TextUnformatted("Frame capture ARMED — tap Space to capture.");
            if (ImGui::Button("Cancel frame arm"))
                sFrameCaptureArmedNextDodge = false;
        }
        else if (sFrameCaptureActive)
        {
            ImGui::Text("Capturing frames... %.2f / %.2fs (%d frames)", sFrameCaptureElapsed,
                        sFrameCaptureDuration, sFrameCaptureCounter);
        }
        else
        {
            if (ImGui::Button("Arm frame capture (next dodge)"))
                sFrameCaptureArmedNextDodge = true;
            ImGui::SameLine();
            if (ImGui::Button("Arm frame capture (next chain)"))
                sFrameCaptureArmedNextChain = true;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Save to config/tunables.json"))
    {
        if (!selva::tuning::saveToFile(kTunablesPath))
            std::fprintf(stderr, "[Tuning] Failed to save %s\n", kTunablesPath.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload from disk"))
        selva::tuning::loadFromFile(kTunablesPath);

    ImGui::End();
}

int main(int /*argc*/, char* /*argv*/[])
{
    // Combat-debug log file: opened at startup when selva::combat::isCombatDebugEnabled()
    // defaults true (iteration mode), or lazily by the F1 toggle when
    // it defaults false (normal play). Truncate on open so each run
    // starts with a fresh log.
    if (selva::combat::isCombatDebugEnabled())
    {
        FILE* log = selva::combat::openCombatLog();
        if (log != nullptr)
            std::fprintf(stderr, "[combat:log] writing diagnostics to combat-debug.log\n");
        // Route sampler-side loco diagnostics (reverse-blend, cache
        // resume, cold-enter) into the same file. PoseSampler doesn't
        // know about combat-debug.log on its own — game wires it.
        selva::anim::setSamplerDiagLog(log);
    }

    Engine engine;

    // 4x MSAA — smooths cube/floor edge silhouettes so they don't crawl
    // when the camera rotates. Tried 8x; visually indistinguishable from
    // 4x at this geometry count, so the extra samples weren't earning
    // their cost. Residual sub-pixel shimmer that MSAA can't fix will be
    // absorbed by the dither/threshold post-process pass when the 1-bit
    // visual identity lands.
    engine.setMSAA(4);

    // Maximized window — full monitor area but keeps title bar / resize
    // handles so the dev can grab and adjust during iteration. The
    // 1280x720 args become the restore size when un-maximized.
    engine.setWindowMode(Engine::WindowMode::Maximized);

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    engine.setClearColor(0.0f, 0.0f, 0.0f);

    // Capture the cursor for mouse-look. SDL_SetRelativeMouseMode hides the
    // cursor and feeds back relative deltas via SDL_GetRelativeMouseState.
    SDL_SetRelativeMouseMode(SDL_TRUE);
    // Drain any startup delta so the first frame's look isn't huge.
    SDL_GetRelativeMouseState(nullptr, nullptr);

    if (!selva::render::initSceneProgram())
    {
        std::fprintf(stderr, "Scene shader compile/link failed\n");
        return 1;
    }

    sWindowW = engine.windowWidth();
    sWindowH = engine.windowHeight();
    selva::render::initSceneGeometry();
    if (!initSkeletalAssets())
    {
        // Non-fatal: the game stays runnable on a fresh checkout where
        // assets haven't been processed. Cube + floor still render; the
        // character just won't appear. Log so we notice if the assets path
        // breaks silently in CI.
        std::fprintf(stderr, "[main] skeletal assets failed to load — character disabled\n");
    }
    else
    {
        // Phase-0 audit for the root-motion refactor: dump per-clip
        // hip path length so we know which clips ship with authored
        // translation. Cheap one-time scan; reasonable to leave in
        // until the refactor lands and then trim.
        auditClipHipMotion();
    }

    // Load runtime-tunable values from JSON. Falls back silently to
    // struct defaults if the file is missing or malformed; the in-game
    // ImGui panel can save updated values back to the same path.
    selva::tuning::loadFromFile(kTunablesPath);

    // Per-clip locomotion blend-in durations. Asymmetric values let
    // Run → Idle settle slowly while Idle → Run snaps fast. Missing
    // file is non-fatal; falls back to tun.anim_blend_seconds for
    // every clip query.
    sLocomotionConfig.loadFromFile("config/locomotion.json");

    // Combat data: weapon classes first (animation/attach data), then
    // weapons (resolve their class pointers against the class registry),
    // then the player's loadout (resolves weapon ids against the weapon
    // registry). Each layer is permissive — missing/unknown ids log a
    // warning but don't crash so the game stays runnable on a fresh
    // checkout. A null hand means "empty/unarmed."
    {
        const int n_classes = sWeaponClasses.loadDirectory(kWeaponClassesDir);
        const int n_weapons = sWeapons.loadDirectory(kWeaponsDir, sWeaponClasses);
        sEquipment = selva::combat::loadEquipment(kLoadoutPath, sWeapons);
        // Synthesize a "fists" Weapon record pointing at the unarmed
        // class; substitute it into any empty hand slot. Equipment is
        // never null after this — empty hand = fists. Stats are
        // intentionally minimal; future work plumbs body-derived
        // unarmed_damage / scaling like prison-escape-game does.
        static selva::combat::Weapon sFistsWeapon;
        const auto unarmed_it = sWeaponClasses.by_id.find("unarmed");
        if (unarmed_it != sWeaponClasses.by_id.end())
        {
            sFistsWeapon.id = "fists";
            sFistsWeapon.name = "Fists";
            sFistsWeapon.class_id = "unarmed";
            sFistsWeapon.cls = &unarmed_it->second;
            if (sEquipment.right == nullptr)
                sEquipment.right = &sFistsWeapon;
            if (sEquipment.left == nullptr)
                sEquipment.left = &sFistsWeapon;
        }
        resolveAttackCancelOpenTimes();
        std::fprintf(
            stderr, "[combat] loaded %d class(es), %d weapon(s); right=%s left=%s grip=%s\n",
            n_classes, n_weapons, sEquipment.right ? sEquipment.right->id.c_str() : "(empty)",
            sEquipment.left ? sEquipment.left->id.c_str() : "(empty)",
            sEquipment.grip == selva::combat::Grip::TwoHanded ? "two_handed" : "one_handed");
    }

    engine.setPerFrameUpdate(&selvaPerFrame);
    engine.setRenderWorld(&selvaRenderWorld);
    engine.setRenderImGui(&selvaRenderImGui);
    engine.setOnResize(&onWindowResize);

    engine.run();

    shutdownSkeletalAssets();
    shutdownGeometry();
    engine.shutdown();
    selva::combat::closeCombatLog();
    return 0;
}
