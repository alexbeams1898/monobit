#include "anim/PoseSampler.h"

#include "anim/AnimationClip.h"
#include "anim/SkeletalMesh.h"
#include "anim/Skeleton.h"

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace selva::anim
{

// Sampler diagnostic log target (set by game code at startup; nullptr
// = stderr only). Used by samplerDiagLog below; called from the loco
// crossfade path to record reverse-blend / cache-resume / cold-enter
// events. Lets a normal launch (no shell stderr capture) still produce
// a usable log via combat-debug.log.
namespace
{
std::FILE* g_diag_log = nullptr;
}

void setSamplerDiagLog(std::FILE* file)
{
    g_diag_log = file;
}

namespace
{
void samplerDiagLog(const char* fmt, ...)
{
    if (g_diag_log == nullptr)
        return;
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_diag_log, fmt, args);
    std::fflush(g_diag_log);
    va_end(args);
}
} // namespace

// ---------------------------------------------------------------------------
// Three-track sampler:
//
//   * loco_current  — the looping locomotion clip the gameplay asked for
//                     this frame (Idle/Walk/Run).
//   * loco_previous — the locomotion clip we're crossfading away from.
//                     Active only while a locomotion change is in flight.
//   * one_shot      — non-looping override clip (attack, dodge, hit
//                     react). When active, dominates the output even
//                     while locomotion changes underneath.
//
// Each track owns its own ozz SamplingJob::Context (heap-backed cache,
// non-movable; that's why all three tracks live as fixed members on Impl
// and we swap their lighter members rather than moving the Track structs).
//
// Per-frame flow:
//   1. Advance time on every track that has an animation (locomotion
//      tracks loop; one-shot doesn't).
//   2. Auto-advance the two blend channels (locomotion-internal + one-shot
//      vs locomotion). When the one-shot's clip nears its end, kick off
//      its blend-out automatically.
//   3. Sample all three tracks.
//   4. First blend: mix loco_current + loco_previous → "locomotion pose".
//   5. Second blend: mix locomotion pose + one-shot pose, weighted by
//      one_shot_weight.
//   6. LocalToModelJob → bone palette.
//
// We always run both blending jobs even when a layer is degenerate
// (weight 0 or null animation); ozz's BlendingJob handles those cleanly
// and keeping the path symmetric avoids special cases.
// ---------------------------------------------------------------------------

namespace
{
struct Track
{
    const ozz::animation::Animation* animation = nullptr;
    // Registry key (e.g. "walking", "running", "unarmed_combat_idle")
    // for this track's bound animation. Used only by diagnostic logs;
    // the Mixamo ozz Animation::name() is always "mixamo.com" so the
    // registry key is the only human-meaningful identifier we have.
    // Empty when no animation bound.
    std::string registry_key;
    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> local_transforms;
    float time_seconds = 0.0f;
    // Cached hip XZ path length for the currently-bound animation.
    // Populated lazily — see PoseSampler::update locomotion-change
    // path. -1 means "not yet computed for this animation."
    // Used by phase-match logic to skip phase-matching when the
    // source or destination clip has no meaningful gait cycle.
    // Also used as the "is this a stationary loop?" filter for
    // velocity-level blend: tracks below the gait-cycle threshold
    // contribute zero hip delta to gameplay translation regardless
    // of any micro-sway in their hip XZ track.
    float hip_path_cached = -1.0f;
    // Per-track hip XZ delta tracking (root-motion source). Each
    // track captures its own clip's pre-freeze hip XZ in model space
    // every frame and computes a per-frame delta. The exposed
    // PoseSampler::consumedHipDelta() blends these per-track deltas
    // by the current blend weights — that's the "velocity-level
    // blend" that prevents phantom backward motion when transitioning
    // from a clip whose hip is at +Z to a clip whose hip is at origin.
    glm::vec2 last_hip_xz = glm::vec2(0.0f);
    bool last_hip_xz_valid = false;
    glm::vec3 last_hip_delta = glm::vec3(0.0f);
    // Non-looping clips (locomotion transitions like start_walking,
    // run_to_stop) clamp at their last frame instead of wrapping back
    // to t=0. `finished` flips true the frame the clip's time reaches
    // its duration; the gameplay state machine reads it to know when
    // the transition is done and the destination loop should begin.
    bool loops = true;
    bool finished = false;

    void resize(int num_joints, int num_soa_joints)
    {
        context.Resize(num_joints);
        local_transforms.resize(num_soa_joints);
    }

    // Reset hip delta tracking. Called on clip change (this frame's
    // delta is forced to zero so we don't emit a pose-snap delta when
    // the clip's authored hip XZ jumps).
    void resetHipTracking()
    {
        last_hip_xz_valid = false;
        last_hip_delta = glm::vec3(0.0f);
    }

    // Sample at the current time. Returns false if the clip is null.
    bool sample()
    {
        if (!animation)
            return false;
        const float dur = animation->duration();
        const float ratio = dur > 0.0f ? (time_seconds / dur) : 0.0f;
        ozz::animation::SamplingJob job;
        job.animation = animation;
        job.context = &context;
        job.ratio = ratio;
        job.output = ozz::make_span(local_transforms);
        return job.Run();
    }
};

enum class OneShotPhase
{
    Inactive, // no one-shot playing
    BlendIn,  // ramping up from 0 to 1
    Hold,     // playing at full weight 1
    BlendOut, // ramping down from 1 to 0; will become Inactive at 0
};
} // namespace

struct PoseSampler::Impl
{
    const ozz::animation::Skeleton* skeleton = nullptr;

    Track loco_current;
    Track loco_previous;
    Track one_shot;

    // Last-seen clip-time per animation. Lets a non-gait clip
    // (combat-stance idle, peaceful idle) resume from where it left
    // off when re-entered, instead of snapping to t=0. unarmed_combat_
    // idle is a 3-second bouncing animation; restarting its time
    // every time the player ping-pongs WASD produces a visible leg
    // spasm. Updated whenever we leave a clip (clip-change swap path
    // OR blend completion), read on re-entry.
    std::unordered_map<const ozz::animation::Animation*, float> last_clip_time;

    // Diagnostic: dedup FROZEN-swap log lines. Stores the
    // "from>to" attempt id of the last suppressed swap so we only
    // emit the line on edge transitions, not every frame the SM
    // retries during full-mask Hold.
    std::string last_frozen_swap_attempt;

    // Locomotion crossfade state. weight = how much of `loco_current` is
    // mixed into the locomotion pose (0 = all previous, 1 = all current).
    // duration = total length of the in-progress fade; elapsed = how far
    // through it we are. When elapsed >= duration the previous track is
    // dropped and weight stays at 1 forever after (until the next change).
    float loco_blend_weight = 1.0f;
    float loco_blend_elapsed = 0.0f;
    float loco_blend_duration = 0.0f;

    // One-shot state. one_shot_weight is how much of the one-shot pose
    // dominates the locomotion pose at the final blend (0 = all loco,
    // 1 = all one-shot). The phase machine drives weight automatically
    // based on the one-shot clip's elapsed time.
    OneShotPhase one_shot_phase = OneShotPhase::Inactive;
    float one_shot_weight = 0.0f;
    float one_shot_blend_in_seconds = 0.0f;
    float one_shot_blend_out_seconds = 0.0f;
    float one_shot_playback_rate = 1.0f;
    PoseSampler::BodyMask one_shot_mask = PoseSampler::BodyMask::Full;
    // When true, time stops advancing once it reaches clip end; the
    // body holds the last frame indefinitely until releaseOneShot()
    // requests blend-out. Used for held actions like unarmed block.
    bool one_shot_freeze_last = false;

    // When true, the freeze gate suppresses loco clip swaps during
    // this one-shot's Hold + BlendOut phases. Set true for dodges/
    // blocks (player wasn't intending a loco-stance change; the
    // one-shot fades back into the prior loco). Set false for
    // attacks (the attack triggers CombatReady and combat-idle
    // should be live by BlendOut so the reveal is in-stance).
    bool one_shot_freeze_loco = false;

    // Previous one-shot track for cancel-into-next-clip crossfading.
    // When playOneShot fires while another one-shot has visible weight,
    // we move the outgoing one-shot here and ramp it down concurrently
    // with the new one-shot's blend-in. Without this, the old one-shot's
    // pose disappears in a single frame (weight slammed to 0), creating
    // a visible pose-snap that inertialization can't bridge cleanly.
    Track one_shot_previous;
    float one_shot_previous_weight = 0.0f;
    float one_shot_previous_fade_seconds = 0.0f;
    float one_shot_previous_playback_rate = 1.0f;
    PoseSampler::BodyMask one_shot_previous_mask = PoseSampler::BodyMask::Full;

    glm::mat4 root_transform = glm::mat4(1.0f);
    std::vector<glm::mat4> inverse_bind_matrices;

    // Per-joint blending mask for upper-body one-shots (attacks). One
    // SimdFloat4 per SoA-joint, each lane = 1.0 if that joint is the
    // Spine or any descendant of it (so the attack drives spine + arms +
    // head), 0.0 otherwise (legs/hips keep playing locomotion). Built
    // once at createPoseSampler from the skeleton hierarchy.
    //
    // Why this matters: without per-joint masking, blending an attack
    // clip over a locomotion clip lerps every joint — including the
    // legs, which the attack clip authored to a standing rest pose.
    // A walking/running character snaps mid-stride to feet-planted-square
    // when LMB is pressed. We solve this by driving only the upper
    // body from the attack track and letting the locomotion track
    // keep cycling the legs.
    std::vector<ozz::math::SimdFloat4> upper_body_weights;

    // Locomotion + combat clips ship with root motion baked into the
    // hips. We use a hybrid extraction:
    //
    //   1. Each frame, read the pre-freeze hip XZ in model space.
    //   2. Compute (this frame XZ) - (last frame XZ) = per-frame motion
    //      delta. This delta represents what the clip wanted to move
    //      the character on this frame.
    //   3. Freeze the hip's XZ in the bone palette back to rest (so the
    //      visual bones don't drag relative to the model anchor).
    //   4. Expose the delta to gameplay code via consumedHipDelta(); it
    //      can apply that delta to sPlayer.pos to advance world position
    //      exactly as the clip authored.
    //
    // Y is left untouched — natural walk/run/roll vertical bob plays.
    //
    // Discontinuities (clip change, one-shot fire, time wrap) emit a
    // zero delta and re-baseline. Without that, the first frame after
    // a discontinuity would emit a giant "delta" reflecting the
    // pose-snap, not real motion.
    //
    // Hip joint index is cached at construction; a value of -1 means
    // "no hip found, skip the freeze" (graceful fallback for non-Mixamo
    // rigs).
    int hips_joint_idx = -1;
    ozz::math::SimdFloat4 hips_rest_translation =
        ozz::math::simd_float4::Load(0.0f, 0.0f, 0.0f, 0.0f);
    // Per-frame hip-XZ tracking now lives on each Track (loco_current,
    // loco_previous, one_shot) so each clip's authored hip motion is
    // computed independently and blended by consumedHipDelta() at the
    // velocity level. See Track::last_hip_xz / last_hip_delta above.

    // Scratch buffer: scaled per-joint weights for the one-shot layer
    // (mask * one_shot_weight). Filled per-frame. Held as a member so we
    // don't allocate every update().
    std::vector<ozz::math::SimdFloat4> one_shot_joint_weights;
    // Same for the outgoing previous one-shot during a cancel crossfade.
    std::vector<ozz::math::SimdFloat4> one_shot_previous_joint_weights;

    // Scratch buffers. `loco_blended` is the output of the loco-internal
    // blend (loco_current + loco_previous). `final_locals` is the output
    // of the one-shot vs locomotion blend, fed into LocalToModelJob.
    std::vector<ozz::math::SoaTransform> loco_blended;
    std::vector<ozz::math::SoaTransform> final_locals;
    std::vector<ozz::math::Float4x4> model_matrices;

    // ---- Inertialization (pose-match) ----
    //
    // When a clip change happens (locomotion swap, one-shot fire,
    // one-shot end), the new clip's t=0 pose almost never matches the
    // pose the player was just seeing. A naive crossfade lerps each
    // joint along the shortest path between two poses, producing
    // visible "skip" — joints teleport over short distances rather
    // than continuing along their existing motion.
    //
    // Inertialization fixes this by capturing the pose at the moment
    // of the change, computing a per-joint offset from the new clip's
    // t=0 pose, and decaying that offset to zero over a short
    // duration. The new clip's authored motion plays as designed; the
    // visible pose = clip motion + decaying offset, so joints curve
    // smoothly from where they were into where the clip wanted them.
    //
    // We capture the post-blend `final_locals` from the previous frame
    // (what was actually rendered) so the offset measures real-vs-
    // authored, not blended-vs-authored.
    //
    // `decay_active` true while a decay is in flight. `decay_elapsed`
    // ticks from 0 → decay_duration in wall-clock seconds.
    // `pre_change_locals` holds the captured pose from the moment of
    // change. `post_locals_last` and `post_locals_prev_last` hold the
    // last two frames' final pose so the next change can capture
    // velocity continuity (currently velocity is approximated from
    // pose-difference; full velocity-aware inertialization is a
    // future upgrade).
    bool decay_active = false;
    // Master elapsed timer — ticks 0 → decay_max_seconds wall-clock,
    // then deactivates. Per-joint windows can complete earlier (their
    // weight clamps to zero); the master timer just bounds the
    // active period.
    float decay_elapsed = 0.0f;
    // Per-joint decay timing. duration is set at capture time per-
    // joint — joints with large pose offsets get proportionally
    // longer windows so their visible motion reads as smooth instead
    // of a fast snap. A single global duration meant a 90° leg
    // rotation traversed in the same time as a 5° finger rotation:
    // the leg looked like a spasm.
    //
    // Storage: one SimdFloat4 per SoA-joint (4 scalar joints packed),
    // matching the SoA layout used everywhere else in this file.
    // Computed in seconds. The decay-application step reads each
    // lane independently when computing the cubic ease-out weight.
    std::vector<ozz::math::SimdFloat4> decay_duration_per_joint;
    std::vector<ozz::math::SimdFloat4> decay_elapsed_per_joint;
    // Fallback / "request" duration: the value supplied to
    // requestInertialization() / set during a clip-change. At capture
    // time, each joint's per-lane duration is scaled relative to this
    // base by its offset magnitude.
    float decay_duration = 0.22f;
    // Per-joint scaling configuration (set via setInertializationScaling).
    // duration_per_joint = base + scale_per_radian * |offset|, clamped
    // to max. base defaults match the legacy single-duration behavior;
    // scale=0 is a kill-switch that returns to the old global model.
    float decay_base_seconds = 0.10f;
    float decay_scale_per_radian = 0.30f;
    float decay_max_seconds = 0.45f;
    bool pending_capture = false; // set by trigger; consumed at end of next update
    // When true, pre_change_locals already holds the canonical source
    // pose (supplied via requestInertializationFromPose). The capture
    // step in update() should NOT overwrite it with post_locals_last.
    bool pending_capture_from_supplied_source = false;
    std::vector<ozz::math::SoaTransform> pre_change_locals;
    std::vector<ozz::math::SoaTransform> post_locals_last;
};

PoseSampler::PoseSampler() : impl(std::make_unique<Impl>())
{
}
PoseSampler::PoseSampler(PoseSampler&&) noexcept = default;
PoseSampler& PoseSampler::operator=(PoseSampler&&) noexcept = default;
PoseSampler::~PoseSampler() = default;

// Build per-joint weights for "upper body only" one-shot blending.
//
// Mask construction is "leg-subtree-out" rather than "spine-subtree-in".
// We start with everything = 1 (attack drives every joint) and then zero
// out the leg subtrees (anything reachable from mixamorig:LeftUpLeg or
// mixamorig:RightUpLeg). This keeps the Hips joint itself driven by the
// attack — critical for swing power, since hip rotation carries the
// entire spine's wind-up. The legs (UpLeg → Leg → Foot → Toes) stay on
// locomotion so a walking/sprinting character doesn't snap to standing.
//
// The boundary: "the legs themselves are locomotion's; everything
// from the pelvis up belongs to the action."
//
// Falls back to all-1.0 (full body) if neither leg root is found —
// better to lose the leg-isolation feature than to silently mute the
// whole upper body on an unfamiliar rig.
static int findJointByName(const ozz::animation::Skeleton& skel, const char* name)
{
    const int n = skel.num_joints();
    const auto names = skel.joint_names();
    for (int i = 0; i < n; ++i)
        if (names[i] != nullptr && std::strcmp(names[i], name) == 0)
            return i;
    return -1;
}

// Per-joint upper-body weights: 1.0 for every joint NOT in the leg
// subtree (left_leg or right_leg as the subtree root), 0.0 inside.
static std::vector<float> computeUpperBodyPerJointWeights(const ozz::animation::Skeleton& skel,
                                                          int left_leg, int right_leg)
{
    const int n = skel.num_joints();
    const auto parents = skel.joint_parents();
    std::vector<float> per_joint(n, 1.0f);
    std::vector<bool> in_leg(n, false);
    if (left_leg >= 0)
        in_leg[left_leg] = true;
    if (right_leg >= 0)
        in_leg[right_leg] = true;
    // ozz guarantees parents come before children — single forward pass
    // propagates the leg-subtree flag down.
    for (int i = 0; i < n; ++i)
    {
        if (parents[i] >= 0 && in_leg[parents[i]])
            in_leg[i] = true;
        if (in_leg[i])
            per_joint[i] = 0.0f;
    }
    return per_joint;
}

// Pack per-joint floats into SoA-aligned SimdFloat4 lanes.
static std::vector<ozz::math::SimdFloat4> packToSoaWeights(const std::vector<float>& per_joint,
                                                           int n_soa)
{
    const int n = static_cast<int>(per_joint.size());
    std::vector<ozz::math::SimdFloat4> weights(n_soa);
    for (int s = 0; s < n_soa; ++s)
    {
        const int base = s * 4;
        const float w0 = base + 0 < n ? per_joint[base + 0] : 0.0f;
        const float w1 = base + 1 < n ? per_joint[base + 1] : 0.0f;
        const float w2 = base + 2 < n ? per_joint[base + 2] : 0.0f;
        const float w3 = base + 3 < n ? per_joint[base + 3] : 0.0f;
        weights[s] = ozz::math::simd_float4::Load(w0, w1, w2, w3);
    }
    return weights;
}

std::vector<ozz::math::SimdFloat4> buildUpperBodyWeights(const ozz::animation::Skeleton& skel)
{
    const int left_leg = findJointByName(skel, "mixamorig:LeftUpLeg");
    const int right_leg = findJointByName(skel, "mixamorig:RightUpLeg");
    const auto per_joint = computeUpperBodyPerJointWeights(skel, left_leg, right_leg);
    const int n = skel.num_joints();
    int n_upper = 0;
    int n_lower = 0;
    for (int i = 0; i < n; ++i)
        (per_joint[i] > 0.5f ? n_upper : n_lower) += 1;
    std::fprintf(stderr,
                 "[PoseSampler] upper-body mask: %d upper / %d lower (LeftUpLeg=%d, "
                 "RightUpLeg=%d)\n",
                 n_upper, n_lower, left_leg, right_leg);
    return packToSoaWeights(per_joint, skel.num_soa_joints());
}

PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh)
{
    PoseSampler sampler;
    if (!skeleton.isLoaded())
        return sampler;

    const ozz::animation::Skeleton& ozz_skel = *skeleton.ozz_skeleton;
    const int n_joints = ozz_skel.num_joints();
    const int n_soa = ozz_skel.num_soa_joints();

    sampler.impl->skeleton = &ozz_skel;
    sampler.impl->loco_current.resize(n_joints, n_soa);
    sampler.impl->loco_previous.resize(n_joints, n_soa);
    sampler.impl->one_shot.resize(n_joints, n_soa);
    sampler.impl->one_shot_previous.resize(n_joints, n_soa);
    sampler.impl->loco_blended.resize(n_soa);
    sampler.impl->final_locals.resize(n_soa);
    sampler.impl->pre_change_locals.resize(n_soa);
    sampler.impl->post_locals_last.resize(n_soa);
    sampler.impl->decay_duration_per_joint.assign(n_soa, ozz::math::simd_float4::zero());
    sampler.impl->decay_elapsed_per_joint.assign(n_soa, ozz::math::simd_float4::zero());
    sampler.impl->model_matrices.resize(n_joints);
    sampler.bone_palette.resize(n_joints);
    sampler.impl->upper_body_weights = buildUpperBodyWeights(ozz_skel);
    sampler.impl->one_shot_joint_weights.resize(n_soa);
    sampler.impl->one_shot_previous_joint_weights.resize(n_soa);

    // Find Hips joint and stash its rest-pose translation (X and Z only;
    // Y is left free so vertical walk-bob still plays). Used per-frame to
    // cancel root motion in locomotion clips — see Impl docstring.
    {
        const int n = ozz_skel.num_joints();
        const auto names = ozz_skel.joint_names();
        for (int i = 0; i < n; ++i)
        {
            if (names[i] != nullptr && std::strcmp(names[i], "mixamorig:Hips") == 0)
            {
                sampler.impl->hips_joint_idx = i;
                // Rest pose is SoA-packed: lane = i % 4 of SoA index i / 4.
                const ozz::math::SoaTransform& T = ozz_skel.joint_rest_poses()[i / 4];
                alignas(16) float xs[4];
                alignas(16) float ys[4];
                alignas(16) float zs[4];
                ozz::math::StorePtr(T.translation.x, xs);
                ozz::math::StorePtr(T.translation.y, ys);
                ozz::math::StorePtr(T.translation.z, zs);
                const int lane = i % 4;
                sampler.impl->hips_rest_translation =
                    ozz::math::simd_float4::Load(xs[lane], ys[lane], zs[lane], 0.0f);
                break;
            }
        }
    }

    sampler.impl->root_transform = mesh.asset_root_transform;
    sampler.impl->inverse_bind_matrices = mesh.inverse_bind_matrices;
    return sampler;
}

void PoseSampler::releaseOneShot()
{
    if (!impl)
        return;
    Impl& s = *impl;
    if (s.one_shot_phase == OneShotPhase::Inactive)
        return;
    s.one_shot_freeze_last = false;
    s.one_shot_phase = OneShotPhase::BlendOut;
}

void PoseSampler::playOneShot(const AnimationClip& clip, float blend_in_seconds,
                              float blend_out_seconds, BodyMask mask, float start_time_seconds,
                              float playback_rate, const OneShotOptions& options)
{
    const bool freeze_last = options.freeze_last;
    const char* clip_key = options.clip_key;
    const bool freeze_loco_during_one_shot = options.freeze_loco_during_one_shot;
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return;

    Impl& s = *impl;
    // If another one-shot is in flight with visible weight, demote it
    // to one_shot_previous and ramp it down concurrently with the new
    // one-shot's blend-in. This is what makes cancel-into-next-clip
    // smooth (jab→hook chain). Without it, slamming one_shot_weight=0
    // on the new fire makes the old one-shot's pose vanish in 1 frame
    // — visible as a leg snap.
    if (s.one_shot_phase != OneShotPhase::Inactive && s.one_shot.animation != nullptr &&
        s.one_shot_weight > 0.001f)
    {
        // Move current one-shot pose into previous track. Sample state
        // (animation, time, locals already populated) carries through.
        s.one_shot_previous.animation = s.one_shot.animation;
        s.one_shot_previous.registry_key = s.one_shot.registry_key;
        s.one_shot_previous.time_seconds = s.one_shot.time_seconds;
        s.one_shot_previous.local_transforms = s.one_shot.local_transforms;
        s.one_shot_previous.last_hip_xz = s.one_shot.last_hip_xz;
        s.one_shot_previous.last_hip_xz_valid = false; // discontinuity
        s.one_shot_previous.last_hip_delta = glm::vec3(0.0f);
        s.one_shot_previous_weight = s.one_shot_weight;
        s.one_shot_previous_fade_seconds = std::max(0.05f, blend_in_seconds);
        s.one_shot_previous_playback_rate = s.one_shot_playback_rate;
        s.one_shot_previous_mask = s.one_shot_mask;
    }
    s.one_shot.animation = clip.ozz_animation.get();
    s.one_shot.registry_key = (clip_key != nullptr) ? clip_key : "";
    const float dur = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
    s.one_shot.time_seconds = std::clamp(start_time_seconds, 0.0f, std::max(0.0f, dur - 1e-4f));
    s.one_shot_phase = OneShotPhase::BlendIn;
    s.one_shot_weight = 0.0f;
    s.one_shot_blend_in_seconds = std::max(0.0f, blend_in_seconds);
    s.one_shot_blend_out_seconds = std::max(0.0f, blend_out_seconds);
    s.one_shot_playback_rate = std::clamp(playback_rate, 0.1f, 10.0f);
    s.one_shot_mask = mask;
    s.one_shot_freeze_last = freeze_last;
    s.one_shot_freeze_loco = freeze_loco_during_one_shot;
    // Discontinuity: pose will pop from locomotion to one-shot. Force
    // next-frame delta to zero so we don't emit a giant pose-snap
    // delta on the one-shot track. (Locomotion tracks keep their own
    // tracking; only the one-shot just got rebound here.)
    s.one_shot.resetHipTracking();
    // playOneShot does NOT enroll inertialization on its own. Callers
    // that want offset-decay-on-top-of-the-clip opt in via an explicit
    // requestInertialization() call BEFORE playOneShot. Earlier this
    // function set pending_capture+decay_duration unconditionally —
    // that silent side effect produced the leg-spasm bug class:
    // every fire enrolled a decay overlay, including paths where the
    // caller had no reason to want one (rapid sprint-finisher fires
    // captured live mid-flying-knee pose and decayed the offset over
    // the next swing's playback, producing visible wrong-leg motion).
}

bool PoseSampler::isOneShotActive() const
{
    if (!impl)
        return false;
    // True from the moment playOneShot() is called through blend-out.
    // The earlier `weight > 0.5` test reported false during the FIRST
    // HALF of the blend-in, which let the gameplay SM keep picking
    // `walking` while a punch was actually being mixed onto the legs —
    // produced a visible leg spasm because mid-stride walking pose
    // and feet-together attack pose blended into each other for ~0.10s.
    return impl->one_shot_phase != OneShotPhase::Inactive;
}

bool PoseSampler::isLocoFrozenByOneShot() const
{
    if (!impl)
        return false;
    // Mirrors the internal isLocoFrozenByFullMaskHold predicate.
    // Active during Hold + BlendOut of full-mask one-shots that
    // OPTED IN to loco freezing (dodges/blocks). See that
    // function's comment for the per-profile rationale.
    const bool fading_or_held = impl->one_shot_phase == OneShotPhase::Hold ||
                                impl->one_shot_phase == OneShotPhase::BlendOut;
    return fading_or_held && impl->one_shot_mask == PoseSampler::BodyMask::Full &&
           impl->one_shot_freeze_loco;
}

bool PoseSampler::locomotionClipFinished() const
{
    if (!impl)
        return false;
    return impl->loco_current.finished;
}

void PoseSampler::requestInertialization(float duration_seconds)
{
    if (!impl)
        return;
    Impl& s = *impl;
    // Mark a capture pending — actual capture happens at the start of
    // the next update() so it picks up the just-rendered final pose.
    // Calling repeatedly in one frame just keeps the request standing;
    // the capture will use whatever post_locals_last holds at that
    // moment.
    s.pending_capture = true;
    s.decay_duration = std::max(0.0f, duration_seconds);
}

void PoseSampler::setInertializationScaling(float base_seconds, float scale_seconds_per_radian,
                                            float max_seconds)
{
    if (!impl)
        return;
    impl->decay_base_seconds = std::max(0.0f, base_seconds);
    impl->decay_scale_per_radian = std::max(0.0f, scale_seconds_per_radian);
    impl->decay_max_seconds = std::max(0.0f, max_seconds);
}

void PoseSampler::requestInertializationFromPose(
    float duration_seconds, const std::vector<ozz::math::SoaTransform>& source_pose)
{
    if (!impl)
        return;
    Impl& s = *impl;
    if (source_pose.size() != s.pre_change_locals.size())
        return; // size mismatch — silently skip; better than corrupting the buffer
    // Copy the supplied pose directly into the pre-change buffer.
    // Mark capture as already done so the apply step computes
    // offset = source - new_clip_t0 (rather than waiting for next-
    // frame's post-blend snapshot). This is the "use a known
    // baseline pose, not whatever was on screen" path — needed for
    // attacks where the visible pose may not be the right reference.
    s.pre_change_locals = source_pose;
    s.pending_capture = false; // already captured here, in canonical form
    s.decay_duration = std::max(0.0f, duration_seconds);
    // Mark "decay needs offset compute on next frame" via a sentinel
    // — we re-use pending_capture for this. Setting both flags would
    // be confusing; instead, signal via a separate flag.
    // Approach: set pending_capture=true but the next-update step
    // skips the post_locals_last copy (since pre_change_locals is
    // already the source). We'll add a small flag to distinguish.
    s.pending_capture = true;
    s.pending_capture_from_supplied_source = true;
}

void PoseSampler::setLocomotionClipTime(float t_seconds)
{
    if (!impl)
        return;
    impl->loco_current.time_seconds = std::max(0.0f, t_seconds);
}

bool PoseSampler::sampleClipPose(const AnimationClip& clip, float t_seconds,
                                 std::vector<ozz::math::SoaTransform>& out) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return false;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return false;
    const float dur = anim->duration();
    if (dur <= 0.0f)
        return false;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    out.resize(n_soa);
    // Scratch sampling context — separate from live tracks, doesn't
    // perturb in-flight animation state.
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    const float ratio = std::clamp(t_seconds / dur, 0.0f, 1.0f);
    ozz::animation::SamplingJob sjob;
    sjob.animation = anim;
    sjob.context = &ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(out);
    return sjob.Run();
}

glm::vec3 PoseSampler::jointWorldPos(int i) const
{
    if (!impl || i < 0 || i >= static_cast<int>(impl->model_matrices.size()))
        return glm::vec3(0.0f);
    glm::mat4 m;
    std::memcpy(&m, &impl->model_matrices[i], sizeof(glm::mat4));
    return glm::vec3(m[3]);
}

int PoseSampler::jointCount() const
{
    if (!impl || impl->skeleton == nullptr)
        return 0;
    return impl->skeleton->num_joints();
}

const char* PoseSampler::jointName(int i) const
{
    if (!impl || impl->skeleton == nullptr || i < 0 || i >= impl->skeleton->num_joints())
        return "";
    return impl->skeleton->joint_names()[i];
}

glm::vec3 PoseSampler::consumedHipDelta() const
{
    if (!impl)
        return glm::vec3(0.0f);
    const Impl& s = *impl;
    // Velocity-level blend: sum each track's clip-authored hip delta,
    // weighted by the same blend weights ozz uses for the pose blend.
    //
    // Locomotion blend: (1 - w) × prev + w × current.
    // One-shot blend: (1 - one_shot_weight) × loco_blended +
    //                 one_shot_weight × one_shot.
    const float w_cur = s.loco_blend_weight;
    const float w_prev = s.loco_previous.animation ? (1.0f - w_cur) : 0.0f;
    const glm::vec3 loco_delta =
        s.loco_current.last_hip_delta * w_cur + s.loco_previous.last_hip_delta * w_prev;
    const float w_os = s.one_shot_weight;
    const glm::vec3 final_delta = loco_delta * (1.0f - w_os) + s.one_shot.last_hip_delta * w_os;
    return final_delta;
}

glm::vec2 PoseSampler::sampleHipXZAt(const AnimationClip& clip, float t_seconds) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded() || impl->hips_joint_idx < 0)
        return glm::vec2(0.0f);
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return glm::vec2(0.0f);
    const float dur = anim->duration();
    if (dur <= 0.0f)
        return glm::vec2(0.0f);

    const ozz::animation::Skeleton& skel = *impl->skeleton;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float ratio = std::clamp(t_seconds / dur, 0.0f, 1.0f);
    ozz::animation::SamplingJob sjob;
    sjob.animation = anim;
    sjob.context = &ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(locals);
    if (!sjob.Run())
        return glm::vec2(0.0f);
    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = &skel;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(locals);
    ljob.output = ozz::make_span(models);
    if (!ljob.Run())
        return glm::vec2(0.0f);
    const ozz::math::Float4x4& m = models[impl->hips_joint_idx];
    alignas(16) float col3[4];
    ozz::math::StorePtr(m.cols[3], col3);
    return glm::vec2(col3[0], col3[2]);
}

namespace
{

// Sample hip XZ positions at fixed time steps across the clip.
// Returns empty on ozz job failure or zero-duration clip.
std::vector<glm::vec2> collectHipXZSamples(const ozz::animation::Animation& anim,
                                           const ozz::animation::Skeleton& skel,
                                           const ozz::math::Float4x4& root_storage, int hips_idx,
                                           float dur, float step, int n_steps)
{
    std::vector<glm::vec2> samples;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);
    samples.reserve(static_cast<std::size_t>(n_steps));
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = &anim;
        sjob.context = &ctx;
        sjob.ratio = ratio;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return {};
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return {};
        const ozz::math::Float4x4& m = models[hips_idx];
        alignas(16) float col3[4];
        ozz::math::StorePtr(m.cols[3], col3);
        samples.emplace_back(col3[0], col3[2]);
    }
    return samples;
}

// Find the peak per-step displacement (proxy for peak hip velocity).
float peakStepDisplacement(const std::vector<glm::vec2>& samples)
{
    float peak = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const float d = glm::length(samples[i] - samples[i - 1]);
        if (d > peak)
            peak = d;
    }
    return peak;
}

// Integrate path length up to the first sample whose per-step
// displacement drops below `quiet_threshold`. Sub-threshold steps
// only count AFTER the peak has occurred, so a slow blend-in at the
// clip start doesn't terminate integration prematurely.
PoseSampler::ClipHipScan integrateUntilQuiet(const std::vector<glm::vec2>& samples, float peak_step,
                                             float quiet_velocity_fraction, float step, float dur)
{
    PoseSampler::ClipHipScan result;
    const float quiet_threshold = peak_step * std::max(0.0f, quiet_velocity_fraction);
    bool seen_peak = false;
    float total = 0.0f;
    float motion_end_t = dur;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const float d = glm::length(samples[i] - samples[i - 1]);
        if (d >= peak_step * 0.9f)
            seen_peak = true;
        if (seen_peak && d < quiet_threshold)
        {
            motion_end_t = static_cast<float>(i) * step;
            break;
        }
        total += d;
    }
    result.path_length = total;
    result.motion_end_time = std::min(motion_end_t, dur);
    return result;
}

} // namespace

PoseSampler::ClipHipScan PoseSampler::clipHipPathLength(const AnimationClip& clip, float sample_hz,
                                                        float quiet_velocity_fraction) const
{
    ClipHipScan result;
    if (!impl || !impl->skeleton || !clip.isLoaded() || impl->hips_joint_idx < 0)
        return result;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return result;
    const float dur = anim->duration();
    if (dur <= 0.0f || sample_hz <= 0.0f)
        return result;

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;

    const std::vector<glm::vec2> samples = collectHipXZSamples(
        *anim, *impl->skeleton, root_storage, impl->hips_joint_idx, dur, step, n_steps);
    if (samples.size() < 2)
        return result;
    return integrateUntilQuiet(samples, peakStepDisplacement(samples), quiet_velocity_fraction,
                               step, dur);
}

// Sample the watched joints' world positions at fixed time steps
// across the clip. Returns one std::vector<glm::vec3> per joint
// (outer index matches `joint_indices`), each holding `n_steps`
// samples at `t = i * step` for i in [0, n_steps). Empty outer vector
// on failure (sampling job didn't run).
std::vector<std::vector<glm::vec3>> collectJointXYZSamples(const ozz::animation::Animation& anim,
                                                           const ozz::animation::Skeleton& skel,
                                                           const ozz::math::Float4x4& root_storage,
                                                           const std::vector<int>& joint_indices,
                                                           float dur, float step, int n_steps)
{
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    std::vector<std::vector<glm::vec3>> joint_samples(joint_indices.size());
    for (auto& v : joint_samples)
        v.reserve(static_cast<std::size_t>(n_steps));

    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = &anim;
        sjob.context = &ctx;
        sjob.ratio = ratio;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return {};
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return {};
        for (std::size_t k = 0; k < joint_indices.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joint_indices[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            joint_samples[k].emplace_back(col3[0], col3[1], col3[2]);
        }
    }
    return joint_samples;
}

// True if any joint index is out of [0, n_joints).
bool anyJointOutOfRange(const std::vector<int>& joint_indices, int n_joints)
{
    for (const int j : joint_indices)
        if (j < 0 || j >= n_joints)
            return true;
    return false;
}

// Reusable scratch state for one-off pose sweeps. Sized once,
// reused across loop iterations so the inner sample call doesn't
// reallocate. Caller is responsible for sizing via `resize`.
struct PoseSampleScratch
{
    ozz::animation::SamplingJob::Context ctx;
    std::vector<ozz::math::SoaTransform> locals;
    std::vector<ozz::math::Float4x4> models;

    void resize(const ozz::animation::Skeleton& skel)
    {
        const int n_joints = skel.num_joints();
        ctx.Resize(n_joints);
        locals.resize(skel.num_soa_joints());
        models.resize(n_joints);
    }
};

// Sample one animation at one time and read the world positions of the
// requested joints. Reuses caller-supplied scratch (so a sweep loop
// doesn't reallocate). Returns false on ozz job failure.
bool samplePoseAtTimeForJoints(const ozz::animation::Animation& anim,
                               const ozz::animation::Skeleton& skel,
                               const ozz::math::Float4x4& root_storage,
                               const std::vector<int>& joint_indices, float t, float dur,
                               PoseSampleScratch& scratch, std::vector<glm::vec3>& out)
{
    const float ratio = std::clamp(t / dur, 0.0f, 1.0f);
    ozz::animation::SamplingJob sjob;
    sjob.animation = &anim;
    sjob.context = &scratch.ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(scratch.locals);
    if (!sjob.Run())
        return false;
    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = &skel;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(scratch.locals);
    ljob.output = ozz::make_span(scratch.models);
    if (!ljob.Run())
        return false;
    out.resize(joint_indices.size());
    for (std::size_t k = 0; k < joint_indices.size(); ++k)
    {
        const ozz::math::Float4x4& m = scratch.models[joint_indices[k]];
        alignas(16) float col3[4];
        ozz::math::StorePtr(m.cols[3], col3);
        out[k] = glm::vec3(col3[0], col3[1], col3[2]);
    }
    return true;
}

// Sweep `next_anim` over [search_window_start, search_window_end] at
// `sample_hz` looking for the time whose joint-set world positions
// most closely match `ref` (squared-distance argmin). Empty window
// returns t_start. Failed samples are skipped, not aborted.
float sweepArgminPoseMatch(const ozz::animation::Animation& next_anim,
                           const ozz::animation::Skeleton& skel,
                           const ozz::math::Float4x4& root_storage,
                           const std::vector<int>& joint_indices, const std::vector<glm::vec3>& ref,
                           float search_window_start, float search_window_end, float sample_hz)
{
    const float next_dur = next_anim.duration();
    const float t_start = std::max(0.0f, search_window_start);
    const float t_end =
        (search_window_end > 0.0f) ? std::min(search_window_end, next_dur) : next_dur;
    if (t_end <= t_start)
        return t_start;
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil((t_end - t_start) / step)) + 1;

    PoseSampleScratch scratch;
    scratch.resize(skel);
    std::vector<glm::vec3> candidate;

    float best_t = t_start;
    float best_dist_sq = std::numeric_limits<float>::infinity();
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(t_start + static_cast<float>(i) * step, t_end);
        if (!samplePoseAtTimeForJoints(next_anim, skel, root_storage, joint_indices, t, next_dur,
                                       scratch, candidate))
            continue;
        float d2 = 0.0f;
        for (std::size_t k = 0; k < ref.size(); ++k)
            d2 += glm::dot(candidate[k] - ref[k], candidate[k] - ref[k]);
        if (d2 < best_dist_sq)
        {
            best_dist_sq = d2;
            best_t = t;
        }
    }
    return best_t;
}

// Per-joint settle time: the first instant after peak velocity when
// step velocity drops below `quiet_threshold`. Returns `dur` if the
// joint never settles. See clipJointMotionEnd's body comment for the
// "mid-follow-through" rationale on why fraction ~0.5 is preferred.
static float jointSettleTime(const std::vector<glm::vec3>& samples, float dur, float step,
                             float quiet_velocity_fraction)
{
    if (samples.size() < 2)
        return 0.0f;
    float peak_step = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        peak_step = std::max(peak_step, glm::length(samples[i] - samples[i - 1]));
    const float quiet_threshold = peak_step * std::max(0.0f, quiet_velocity_fraction);
    bool seen_peak = false;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const float d = glm::length(samples[i] - samples[i - 1]);
        if (d >= peak_step * 0.9f)
            seen_peak = true;
        if (seen_peak && d < quiet_threshold)
            return static_cast<float>(i) * step;
    }
    return dur;
}

float PoseSampler::clipJointMotionEnd(const AnimationClip& clip,
                                      const std::vector<int>& joint_indices, float sample_hz,
                                      float quiet_velocity_fraction) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded() || joint_indices.empty())
        return 0.0f;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return 0.0f;
    const float dur = anim->duration();
    if (dur <= 0.0f || sample_hz <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (anyJointOutOfRange(joint_indices, skel.num_joints()))
        return dur;

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;
    const auto joint_samples =
        collectJointXYZSamples(*anim, skel, root_storage, joint_indices, dur, step, n_steps);
    if (joint_samples.empty())
        return dur;

    // Watched-joint settle time is the MAX across joints — every
    // watched joint must have stilled.
    float settle_time = 0.0f;
    for (const auto& samples : joint_samples)
        settle_time =
            std::max(settle_time, jointSettleTime(samples, dur, step, quiet_velocity_fraction));
    return std::min(settle_time, dur);
}

// Per-joint motion-start: first instant when step velocity crosses
// `start_threshold` (fraction of joint's peak velocity).
static float jointMotionStartTime(const std::vector<glm::vec3>& samples, float step,
                                  float start_velocity_fraction)
{
    if (samples.size() < 2)
        return 0.0f;
    float peak_step = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        peak_step = std::max(peak_step, glm::length(samples[i] - samples[i - 1]));
    const float start_threshold = peak_step * std::max(0.0f, start_velocity_fraction);
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        if (glm::length(samples[i] - samples[i - 1]) >= start_threshold)
            return static_cast<float>(i) * step;
    }
    return 0.0f;
}

float PoseSampler::clipJointMotionStart(const AnimationClip& clip,
                                        const std::vector<int>& joint_indices, float sample_hz,
                                        float start_velocity_fraction) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded() || joint_indices.empty())
        return 0.0f;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return 0.0f;
    const float dur = anim->duration();
    if (dur <= 0.0f || sample_hz <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (anyJointOutOfRange(joint_indices, skel.num_joints()))
        return 0.0f;

    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;
    const auto joint_samples =
        collectJointXYZSamples(*anim, skel, root_storage, joint_indices, dur, step, n_steps);
    if (joint_samples.empty())
        return 0.0f;

    // Earliest moment ANY watched joint enters its active phase
    // (MIN, not MAX — chain link should fire as soon as the first
    // joint starts contributing to the swing).
    float earliest_start = dur;
    for (const auto& samples : joint_samples)
        earliest_start =
            std::min(earliest_start, jointMotionStartTime(samples, step, start_velocity_fraction));
    return std::min(earliest_start, dur);
}

// Per-joint argmax velocity time (when step velocity is highest).
static float jointPeakTime(const std::vector<glm::vec3>& samples, float step)
{
    if (samples.size() < 2)
        return 0.0f;
    float peak_step = 0.0f;
    float peak_t = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const float d = glm::length(samples[i] - samples[i - 1]);
        if (d > peak_step)
        {
            peak_step = d;
            peak_t = static_cast<float>(i) * step;
        }
    }
    return peak_t;
}

float PoseSampler::clipJointMotionPeak(const AnimationClip& clip,
                                       const std::vector<int>& joint_indices, float sample_hz) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded() || joint_indices.empty())
        return 0.0f;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return 0.0f;
    const float dur = anim->duration();
    if (dur <= 0.0f || sample_hz <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (anyJointOutOfRange(joint_indices, skel.num_joints()))
        return 0.5f * dur;

    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;
    const auto joint_samples =
        collectJointXYZSamples(*anim, skel, root_storage, joint_indices, dur, step, n_steps);
    if (joint_samples.empty())
        return 0.5f * dur;

    // Latest peak across joints — past this, all watched joints
    // are decelerating into recovery.
    float latest_peak = 0.0f;
    for (const auto& samples : joint_samples)
        latest_peak = std::max(latest_peak, jointPeakTime(samples, step));
    return std::min(latest_peak, dur);
}

float PoseSampler::clipPoseMatchTime(const AnimationClip& prev_clip, float prev_t_seconds,
                                     const AnimationClip& next_clip,
                                     const std::vector<int>& joint_indices,
                                     float search_window_start, float search_window_end,
                                     float sample_hz) const
{
    if (!impl || !impl->skeleton || !prev_clip.isLoaded() || !next_clip.isLoaded() ||
        joint_indices.empty())
        return 0.0f;
    const ozz::animation::Animation* prev_anim = prev_clip.ozz_animation.get();
    const ozz::animation::Animation* next_anim = next_clip.ozz_animation.get();
    if (prev_anim == nullptr || next_anim == nullptr)
        return 0.0f;
    const float next_dur = next_anim->duration();
    if (next_dur <= 0.0f || sample_hz <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (anyJointOutOfRange(joint_indices, skel.num_joints()))
        return 0.0f;

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    // Reference pose: prev clip at prev_t_seconds.
    PoseSampleScratch ref_scratch;
    ref_scratch.resize(skel);
    std::vector<glm::vec3> ref;
    if (!samplePoseAtTimeForJoints(*prev_anim, skel, root_storage, joint_indices, prev_t_seconds,
                                   prev_anim->duration(), ref_scratch, ref))
        return 0.0f;

    return sweepArgminPoseMatch(*next_anim, skel, root_storage, joint_indices, ref,
                                search_window_start, search_window_end, sample_hz);
}

float PoseSampler::clipPoseMatchTime(const std::vector<glm::vec3>& ref_world_pos,
                                     const AnimationClip& next_clip,
                                     const std::vector<int>& joint_indices,
                                     float search_window_start, float search_window_end,
                                     float sample_hz) const
{
    if (!impl || !impl->skeleton || !next_clip.isLoaded() || joint_indices.empty() ||
        ref_world_pos.size() != joint_indices.size())
        return 0.0f;
    const ozz::animation::Animation* next_anim = next_clip.ozz_animation.get();
    if (next_anim == nullptr)
        return 0.0f;
    const float next_dur = next_anim->duration();
    if (next_dur <= 0.0f || sample_hz <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (anyJointOutOfRange(joint_indices, skel.num_joints()))
        return 0.0f;

    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));
    return sweepArgminPoseMatch(*next_anim, skel, root_storage, joint_indices, ref_world_pos,
                                search_window_start, search_window_end, sample_hz);
}

glm::vec3 PoseSampler::clipJointVelocityAt(const AnimationClip& clip, float t_seconds,
                                           const std::vector<int>& joint_indices) const
{
    if (joint_indices.empty())
        return glm::vec3(0.0f);
    const float dur = clip.isLoaded() ? clip.ozz_animation->duration() : 0.0f;
    if (dur <= 0.0f)
        return glm::vec3(0.0f);
    const float dt_frame = 1.0f / 60.0f;
    const float t0 = std::clamp(t_seconds, 0.0f, std::max(0.0f, dur - dt_frame));
    const float t1 = std::min(t0 + dt_frame, dur);
    glm::vec3 sum(0.0f);
    for (const int j : joint_indices)
    {
        const glm::vec3 a = sampleJointWorldPos(clip, t0, j);
        const glm::vec3 b = sampleJointWorldPos(clip, t1, j);
        sum += (b - a);
    }
    return sum;
}

float PoseSampler::clipVelocityMatchTime(const glm::vec3& reference_velocity,
                                         const AnimationClip& next_clip,
                                         const std::vector<int>& joint_indices,
                                         float search_window_start, float search_window_end,
                                         float sample_hz) const
{
    if (!impl || !impl->skeleton || !next_clip.isLoaded() || joint_indices.empty())
        return search_window_start;
    const float ref_len = glm::length(reference_velocity);
    if (ref_len < 1e-5f)
        return search_window_start;
    const float dur = next_clip.ozz_animation->duration();
    if (dur <= 0.0f || sample_hz <= 0.0f)
        return search_window_start;

    const float t_start = std::max(0.0f, search_window_start);
    const float t_end = (search_window_end > 0.0f) ? std::min(search_window_end, dur) : dur;
    if (t_end <= t_start)
        return t_start;
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil((t_end - t_start) / step)) + 1;

    const glm::vec3 ref_dir = reference_velocity / ref_len;
    float best_dot = -2.0f; // -1 = opposite, +1 = aligned
    float best_t = t_start;
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(t_start + static_cast<float>(i) * step, t_end);
        const glm::vec3 v = clipJointVelocityAt(next_clip, t, joint_indices);
        const float vlen = glm::length(v);
        if (vlen < 1e-5f)
            continue;
        const float d = glm::dot(v / vlen, ref_dir);
        if (d > best_dot)
        {
            best_dot = d;
            best_t = t;
        }
    }
    return best_t;
}

glm::vec3 PoseSampler::sampleJointWorldPos(const AnimationClip& clip, float t_seconds,
                                           int joint_idx) const
{
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return glm::vec3(0.0f);
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return glm::vec3(0.0f);
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    if (joint_idx < 0 || joint_idx >= skel.num_joints())
        return glm::vec3(0.0f);
    const float dur = anim->duration();
    if (dur <= 0.0f)
        return glm::vec3(0.0f);

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(skel.num_joints());
    std::vector<ozz::math::SoaTransform> locals(skel.num_soa_joints());
    std::vector<ozz::math::Float4x4> models(skel.num_joints());
    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float ratio = std::clamp(t_seconds / dur, 0.0f, 1.0f);
    ozz::animation::SamplingJob sjob;
    sjob.animation = anim;
    sjob.context = &ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(locals);
    if (!sjob.Run())
        return glm::vec3(0.0f);

    // Apply the runtime's hip-XZ freeze: the live update() pipeline
    // overrides the hip joint's translation X and Z back to the rest
    // pose every frame so the visible bones stay anchored at the
    // model origin. Without applying the same override here, samples
    // come out in *clip-authored* hip-translated space — which is a
    // different coordinate frame from jointWorldPos() and produces
    // bogus splice / bookend deltas for any joint downstream of the
    // hip (i.e. essentially every joint).
    if (impl->hips_joint_idx >= 0)
    {
        const int hip_soa = impl->hips_joint_idx / 4;
        const int hip_lane = impl->hips_joint_idx % 4;
        ozz::math::SoaTransform& T = locals[hip_soa];
        alignas(16) float tx[4];
        alignas(16) float tz[4];
        ozz::math::StorePtr(T.translation.x, tx);
        ozz::math::StorePtr(T.translation.z, tz);
        alignas(16) float rest[4];
        ozz::math::StorePtr(impl->hips_rest_translation, rest);
        tx[hip_lane] = rest[0];
        tz[hip_lane] = rest[2];
        T.translation.x = ozz::math::simd_float4::Load(tx[0], tx[1], tx[2], tx[3]);
        T.translation.z = ozz::math::simd_float4::Load(tz[0], tz[1], tz[2], tz[3]);
    }

    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = &skel;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(locals);
    ljob.output = ozz::make_span(models);
    if (!ljob.Run())
        return glm::vec3(0.0f);

    alignas(16) float col3[4];
    ozz::math::StorePtr(models[joint_idx].cols[3], col3);
    return glm::vec3(col3[0], col3[1], col3[2]);
}

int PoseSampler::findJoint(const char* name) const
{
    if (!impl || !impl->skeleton || name == nullptr)
        return -1;
    const ozz::animation::Skeleton& skel = *impl->skeleton;
    const int n = skel.num_joints();
    const auto names = skel.joint_names();
    for (int i = 0; i < n; ++i)
        if (names[i] != nullptr && std::strcmp(names[i], name) == 0)
            return i;
    return -1;
}

PoseSampler::FrameDiagnostics PoseSampler::frameDiagnostics() const
{
    FrameDiagnostics d{};
    if (!impl)
        return d;
    const Impl& s = *impl;
    // Prefer the registry key (e.g. "walking") over ozz Animation
    // name (always "mixamo.com" for Mixamo clips). Empty key falls
    // back to ozz name so partially-instrumented call sites still
    // produce something.
    d.loco_current_name =
        !s.loco_current.registry_key.empty()
            ? s.loco_current.registry_key.c_str()
            : (s.loco_current.animation ? s.loco_current.animation->name() : nullptr);
    d.loco_previous_name =
        !s.loco_previous.registry_key.empty()
            ? s.loco_previous.registry_key.c_str()
            : (s.loco_previous.animation ? s.loco_previous.animation->name() : nullptr);
    d.one_shot_name = !s.one_shot.registry_key.empty()
                          ? s.one_shot.registry_key.c_str()
                          : (s.one_shot.animation ? s.one_shot.animation->name() : nullptr);
    d.loco_current_time = s.loco_current.time_seconds;
    d.loco_blend_weight = s.loco_blend_weight;
    d.loco_blend_elapsed = s.loco_blend_elapsed;
    d.loco_blend_duration = s.loco_blend_duration;
    d.one_shot_weight = s.one_shot_weight;
    d.one_shot_phase = static_cast<int>(s.one_shot_phase);
    d.one_shot_time = s.one_shot.time_seconds;
    d.one_shot_duration = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
    // Per-track tracking now: report the loco_current track's snapshot
    // since that's the most relevant single track for diagnostics
    // (the dominant locomotion clip during normal play).
    d.last_hip_xz_valid = s.loco_current.last_hip_xz_valid;
    d.last_hip_xz_x = s.loco_current.last_hip_xz.x;
    d.last_hip_xz_z = s.loco_current.last_hip_xz.y;
    d.last_hip_delta_x = s.loco_current.last_hip_delta.x;
    d.last_hip_delta_z = s.loco_current.last_hip_delta.z;
    return d;
}

namespace
{

// Forward decl — defined in the second anonymous-namespace block
// further down. Needed by handleLocoClipChange.
float computeHipXZPathLength(const PoseSampler::Impl& s, const ozz::animation::Animation* anim);

// Find joint by Mixamo name in the skeleton.
int findSkeletonJoint(const ozz::animation::Skeleton& skel, const char* name)
{
    for (int j = 0; j < skel.num_joints(); ++j)
        if (std::strcmp(skel.joint_names()[j], name) == 0)
            return j;
    return -1;
}

// Sample the outgoing loco track in isolation (no one-shot, no
// crossfade) at its current time, then extract world positions for
// the named joints. Used as the splice-time pose-match reference at
// the loco-clip-change branch — the loco track's own pose is the
// correct reference (the live blended pose includes one-shot fade
// contamination).
//
// Returns true on success and fills `joints` + `ref` (parallel
// vectors). Returns false if any sampling step fails.
bool sampleLocoTrackJoints(const PoseSampler::Impl& s, std::vector<int>& joints,
                           std::vector<glm::vec3>& ref)
{
    if (s.loco_current.animation == nullptr)
        return false;
    const ozz::animation::Skeleton& skel = *s.skeleton;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);
    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &s.root_transform, sizeof(glm::mat4));
    const float prev_dur = s.loco_current.animation->duration();
    const float ratio =
        (prev_dur > 0.0f) ? std::clamp(s.loco_current.time_seconds / prev_dur, 0.0f, 1.0f) : 0.0f;
    ozz::animation::SamplingJob sjob;
    sjob.animation = s.loco_current.animation;
    sjob.context = &ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(locals);
    if (!sjob.Run())
        return false;
    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = &skel;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(locals);
    ljob.output = ozz::make_span(models);
    if (!ljob.Run())
        return false;
    const char* names[] = {"mixamorig:LeftUpLeg", "mixamorig:RightUpLeg", "mixamorig:LeftFoot",
                           "mixamorig:RightFoot"};
    for (const char* n : names)
    {
        const int idx = findSkeletonJoint(skel, n);
        if (idx < 0)
            continue;
        joints.push_back(idx);
        const ozz::math::Float4x4& m = models[idx];
        alignas(16) float col3[4];
        ozz::math::StorePtr(m.cols[3], col3);
        ref.emplace_back(col3[0], col3[1], col3[2]);
    }
    return true;
}

// Reverse-blend: `desired` is the clip we were blending FROM. Swap
// tracks, flip the weight, preserve loco_previous's time so the
// gait/bounce doesn't visibly restart. Used for rapid WASD ping-
// pong (idle ↔ walking ↔ idle ↔ walking) — without this, every
// swap snaps unarmed_combat_idle's time to 0 and creates a leg
// spasm because the bounce pose differs at each tap.
void applyLocoReverseBlend(PoseSampler::Impl& s)
{
    const char* cur_key =
        s.loco_current.registry_key.empty() ? "(unknown)" : s.loco_current.registry_key.c_str();
    const char* prev_key =
        s.loco_previous.registry_key.empty() ? "(unknown)" : s.loco_previous.registry_key.c_str();
    samplerDiagLog("[loco] reverse-blend %s <-> %s (weight %.2f -> %.2f, t=%.3fs)\n", cur_key,
                   prev_key, s.loco_blend_weight, 1.0f - s.loco_blend_weight,
                   s.loco_previous.time_seconds);
    std::swap(s.loco_current.animation, s.loco_previous.animation);
    std::swap(s.loco_current.registry_key, s.loco_previous.registry_key);
    std::swap(s.loco_current.time_seconds, s.loco_previous.time_seconds);
    std::swap(s.loco_current.hip_path_cached, s.loco_previous.hip_path_cached);
    s.loco_current.local_transforms.swap(s.loco_previous.local_transforms);
    s.loco_blend_weight = 1.0f - s.loco_blend_weight;
    s.loco_blend_elapsed = std::max(0.0f, s.loco_blend_duration - s.loco_blend_elapsed);
    s.loco_current.finished = false;
}

// Forward-blend: rebind loco_current to `desired` at `new_start_time`
// while preserving the OLD loco_current as loco_previous (which
// fades out over `blend_seconds`).
void applyLocoForwardBlend(PoseSampler::Impl& s, const ozz::animation::Animation* desired,
                           const std::string& desired_key, float new_start_time,
                           float new_clip_path, float blend_seconds)
{
    std::swap(s.loco_current.animation, s.loco_previous.animation);
    std::swap(s.loco_current.registry_key, s.loco_previous.registry_key);
    std::swap(s.loco_current.time_seconds, s.loco_previous.time_seconds);
    std::swap(s.loco_current.hip_path_cached, s.loco_previous.hip_path_cached);
    s.loco_current.local_transforms.swap(s.loco_previous.local_transforms);
    s.loco_blend_weight = 0.0f;
    s.loco_blend_elapsed = 0.0f;
    s.loco_blend_duration = blend_seconds;
    s.loco_current.animation = desired;
    s.loco_current.registry_key = desired_key;
    s.loco_current.time_seconds = new_start_time;
    s.loco_current.hip_path_cached = new_clip_path;
    s.loco_current.finished = false;
    s.loco_current.resetHipTracking();
}

// Cold-enter (no previous loco clip): resume from cache if we played
// `desired` before, else start at 0. Pose-match has no live reference
// on the very first clip.
float locoColdEnterStartTime(const PoseSampler::Impl& s, const ozz::animation::Animation* desired,
                             const char* desired_key, float new_dur)
{
    const auto it = s.last_clip_time.find(desired);
    if (it != s.last_clip_time.end())
    {
        const float t = std::fmod(it->second, std::max(new_dur, 1e-4f));
        samplerDiagLog("[loco] resume %s @ cached t=%.3fs (cold)\n", desired_key, t);
        return t;
    }
    samplerDiagLog("[loco] cold-enter %s @ t=0\n", desired_key);
    return 0.0f;
}

// Apply a clean (non-crossfade) loco rebind: clear loco_previous and
// snap loco_current to `desired` at `new_start_time`. Used when blend
// is disabled or there's no outgoing clip to fade from.
void applyLocoSnap(PoseSampler::Impl& s, const ozz::animation::Animation* desired,
                   const std::string& desired_key, float new_start_time, float new_clip_path)
{
    s.loco_previous.animation = nullptr;
    s.loco_previous.registry_key.clear();
    s.loco_previous.hip_path_cached = -1.0f;
    s.loco_blend_weight = 1.0f;
    s.loco_blend_elapsed = 0.0f;
    s.loco_blend_duration = 0.0f;
    s.loco_current.animation = desired;
    s.loco_current.registry_key = desired_key;
    s.loco_current.time_seconds = new_start_time;
    s.loco_current.hip_path_cached = new_clip_path;
    s.loco_current.finished = false;
    s.loco_current.resetHipTracking();
    s.pending_capture = true;
    s.decay_duration = 0.0f;
}

// The full locomotion-clip-change branch: stash old time, resolve
// hip-path cache, pose-match new start time, then route to the
// appropriate blend (reverse / forward / cold snap).
void handleLocoClipChange(const PoseSampler& sampler, PoseSampler::Impl& s,
                          const AnimationClip& clip, const ozz::animation::Animation* desired,
                          const std::string& desired_key, float blend_seconds)
{
    if (s.loco_current.animation != nullptr)
        s.last_clip_time[s.loco_current.animation] = s.loco_current.time_seconds;
    if (s.loco_current.hip_path_cached < 0.0f && s.loco_current.animation != nullptr)
        s.loco_current.hip_path_cached = computeHipXZPathLength(s, s.loco_current.animation);
    const float new_clip_path = computeHipXZPathLength(s, desired);
    (void)new_clip_path; // computed for cache side-effect; legacy phase-match metric

    float new_start_time = 0.0f;
    const float new_dur = desired->duration();
    const char* to_key = desired_key.empty() ? "(unknown)" : desired_key.c_str();
    const char* from_key =
        s.loco_current.registry_key.empty() ? "(none)" : s.loco_current.registry_key.c_str();

    // Pose-match the new clip's start to the outgoing loco track's
    // current legs+feet pose. The loco track in isolation is the
    // correct reference (not the live blended pose, which during a
    // one-shot fade-out blends in the one-shot's contribution).
    std::vector<int> joints;
    std::vector<glm::vec3> ref;
    if (s.loco_current.animation != nullptr && new_dur > 0.0f)
        sampleLocoTrackJoints(s, joints, ref);
    if (!joints.empty())
    {
        new_start_time = sampler.clipPoseMatchTime(ref, clip, joints, 0.0f, new_dur);
        samplerDiagLog("[loco] pose-match %s @ t=%.3fs (prev=%s, prev_t=%.3fs)\n", to_key,
                       new_start_time, from_key, s.loco_current.time_seconds);
        for (std::size_t k = 0; k < joints.size(); ++k)
        {
            samplerDiagLog("  [loco-iso] joint=%s ref=(%.2f,%.2f,%.2f)\n",
                           s.skeleton->joint_names()[joints[k]], ref[k].x, ref[k].y, ref[k].z);
        }
    }
    else if (s.loco_current.animation == nullptr)
    {
        new_start_time = locoColdEnterStartTime(s, desired, to_key, new_dur);
    }

    if (blend_seconds > 0.0f && s.loco_current.animation != nullptr)
    {
        const bool reverse_blend = (s.loco_previous.animation == desired);
        if (reverse_blend)
            applyLocoReverseBlend(s);
        else
            applyLocoForwardBlend(s, desired, desired_key, new_start_time, new_clip_path,
                                  blend_seconds);
        s.pending_capture = true;
        s.decay_duration = std::max(0.0f, blend_seconds);
    }
    else
    {
        applyLocoSnap(s, desired, desired_key, new_start_time, new_clip_path);
    }
}

// Forward decl — defined further down with the rest of the
// one-shot-phase helpers so it can sit next to its conceptual
// neighbors. routeLocoClipChange below needs to call it.
bool isLocoFrozenByFullMaskHold(const PoseSampler::Impl& s);

// Inertialization step 1 — consume the pending_capture flag if set.
// Two modes:
//   * Default: snapshot the previous frame's rendered pose
//     (post_locals_last) into pre_change_locals. After the blend,
//     compute offset = source - new_pose. Right for locomotion.
//   * From-supplied: pre_change_locals is already the canonical
//     source pose (set by requestInertializationFromPose). Skip the
//     snapshot — go straight to offset compute.
// Returns whether a capture should fire this frame.
bool consumeInertializationCaptureFlag(PoseSampler::Impl& s)
{
    if (!s.pending_capture)
        return false;
    if (!s.pending_capture_from_supplied_source)
        s.pre_change_locals = s.post_locals_last;
    s.pending_capture = false;
    s.pending_capture_from_supplied_source = false;
    return true;
}

// Suppressed loco-clip-change during full-mask Hold. Logs the gate
// firing edge-only (first frame of a given (from,to) attempt) so we
// don't spam every frame the SM retries the swap.
void logFrozenSwap(PoseSampler::Impl& s, const char* from_key, const char* to_key)
{
    const std::string attempt_id = std::string(from_key) + ">" + to_key;
    if (s.last_frozen_swap_attempt == attempt_id)
        return;
    samplerDiagLog("[loco] FROZEN swap %s -> %s (full-mask Hold; will resume at BlendOut)\n",
                   from_key, to_key);
    s.last_frozen_swap_attempt = attempt_id;
}

// Decide whether a loco-clip-change request is suppressed (full-mask
// Hold) or allowed through. Either logs the FROZEN edge or kicks the
// real handleLocoClipChange.
void routeLocoClipChange(const PoseSampler& sampler, PoseSampler::Impl& s,
                         const AnimationClip& clip, const ozz::animation::Animation* desired,
                         const std::string& desired_key, float blend_seconds)
{
    const char* from_key =
        s.loco_current.registry_key.empty() ? "(none)" : s.loco_current.registry_key.c_str();
    const char* to_key = desired_key.empty() ? "(none)" : desired_key.c_str();
    if (isLocoFrozenByFullMaskHold(s))
    {
        logFrozenSwap(s, from_key, to_key);
        return;
    }
    s.last_frozen_swap_attempt.clear();
    handleLocoClipChange(sampler, s, clip, desired, desired_key, blend_seconds);
}

// Inertialization step 2 capture: pre_change_locals currently holds the
// previous-frame pose; subtract the post-blend pose to get the per-joint
// offset. Stored back into pre_change_locals (now repurposed as the
// static offset). Also computes per-joint decay duration window from
// |offset_radians|, scaled by base/scale/max tunables. Starts the
// decay timer.
void captureInertializationOffset(PoseSampler::Impl& s)
{
    const std::size_t n_soa = s.final_locals.size();
    const ozz::math::SimdFloat4 base_simd = ozz::math::simd_float4::Load1(s.decay_base_seconds);
    const ozz::math::SimdFloat4 scale_simd =
        ozz::math::simd_float4::Load1(s.decay_scale_per_radian);
    const ozz::math::SimdFloat4 max_simd = ozz::math::simd_float4::Load1(s.decay_max_seconds);
    const ozz::math::SimdFloat4 two_simd = ozz::math::simd_float4::Load1(2.0f);
    for (std::size_t i = 0; i < n_soa; ++i)
    {
        const ozz::math::SoaTransform& src = s.pre_change_locals[i];
        const ozz::math::SoaTransform& tgt = s.final_locals[i];
        ozz::math::SoaTransform off;
        off.translation.x = src.translation.x - tgt.translation.x;
        off.translation.y = src.translation.y - tgt.translation.y;
        off.translation.z = src.translation.z - tgt.translation.z;
        ozz::math::SoaQuaternion inv_tgt;
        inv_tgt.x = -tgt.rotation.x;
        inv_tgt.y = -tgt.rotation.y;
        inv_tgt.z = -tgt.rotation.z;
        inv_tgt.w = tgt.rotation.w;
        const auto& a = src.rotation;
        const auto& b = inv_tgt;
        off.rotation.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        off.rotation.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        off.rotation.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        off.rotation.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        off.scale = src.scale;
        s.pre_change_locals[i] = off;

        const ozz::math::SimdFloat4 xyz_sq = off.rotation.x * off.rotation.x +
                                             off.rotation.y * off.rotation.y +
                                             off.rotation.z * off.rotation.z;
        const ozz::math::SimdFloat4 eps = ozz::math::simd_float4::Load1(1e-8f);
        const ozz::math::SimdFloat4 safe = xyz_sq + eps;
        const ozz::math::SimdFloat4 inv_len_xyz = ozz::math::RSqrtEst(safe);
        const ozz::math::SimdFloat4 xyz_len = safe * inv_len_xyz;
        const ozz::math::SimdFloat4 angle = two_simd * xyz_len;
        ozz::math::SimdFloat4 dur = base_simd + scale_simd * angle;
        dur = ozz::math::Min(dur, max_simd);
        s.decay_duration_per_joint[i] = dur;
        s.decay_elapsed_per_joint[i] = ozz::math::simd_float4::zero();
    }
    s.decay_active = true;
    s.decay_elapsed = 0.0f;
}

// Inertialization step 2 apply: cubic-ease-out offset onto final_locals
// over the per-joint decay window. Quat-mul: out_q = NLerp(identity,
// offset_q, w) * clip_q. Translation: linear add of weighted offset.
void applyInertializationDecay(PoseSampler::Impl& s, float dt)
{
    s.decay_elapsed += dt;
    if (s.decay_elapsed >= s.decay_max_seconds)
    {
        s.decay_active = false;
        return;
    }
    const ozz::math::SimdFloat4 dt_simd = ozz::math::simd_float4::Load1(dt);
    const ozz::math::SimdFloat4 zero_simd = ozz::math::simd_float4::zero();
    const ozz::math::SimdFloat4 one_simd = ozz::math::simd_float4::Load1(1.0f);
    const std::size_t n_soa = s.final_locals.size();
    for (std::size_t i = 0; i < n_soa; ++i)
    {
        s.decay_elapsed_per_joint[i] = s.decay_elapsed_per_joint[i] + dt_simd;
        const ozz::math::SimdFloat4 dur = s.decay_duration_per_joint[i];
        const ozz::math::SimdFloat4 elapsed = s.decay_elapsed_per_joint[i];
        const ozz::math::SimdFloat4 dur_safe =
            ozz::math::Max(dur, ozz::math::simd_float4::Load1(1e-6f));
        ozz::math::SimdFloat4 u = one_simd - elapsed * ozz::math::RcpEst(dur_safe);
        u = ozz::math::Max(u, zero_simd);
        u = ozz::math::Min(u, one_simd);
        ozz::math::SimdFloat4 w_simd = u * u * u;
        const ozz::math::SimdInt4 dur_nonzero =
            ozz::math::CmpGt(dur, ozz::math::simd_float4::Load1(1e-7f));
        w_simd = ozz::math::Select(dur_nonzero, w_simd, zero_simd);

        const ozz::math::SoaTransform& off = s.pre_change_locals[i];
        ozz::math::SoaTransform& cur = s.final_locals[i];
        cur.translation.x = cur.translation.x + off.translation.x * w_simd;
        cur.translation.y = cur.translation.y + off.translation.y * w_simd;
        cur.translation.z = cur.translation.z + off.translation.z * w_simd;
        ozz::math::SoaQuaternion decayed_off;
        decayed_off.x = off.rotation.x * w_simd;
        decayed_off.y = off.rotation.y * w_simd;
        decayed_off.z = off.rotation.z * w_simd;
        decayed_off.w = (one_simd - w_simd) + off.rotation.w * w_simd;
        const ozz::math::SimdFloat4 dot =
            decayed_off.x * decayed_off.x + decayed_off.y * decayed_off.y +
            decayed_off.z * decayed_off.z + decayed_off.w * decayed_off.w;
        const ozz::math::SimdFloat4 inv_len = ozz::math::RSqrtEst(dot);
        decayed_off.x = decayed_off.x * inv_len;
        decayed_off.y = decayed_off.y * inv_len;
        decayed_off.z = decayed_off.z * inv_len;
        decayed_off.w = decayed_off.w * inv_len;
        const auto& a = decayed_off;
        const auto& b = cur.rotation;
        ozz::math::SoaQuaternion out;
        out.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        out.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        out.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        out.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        cur.rotation = out;
    }
}

// Total hip XZ path length traversed by `anim`, sampled at 60Hz.
// Used by the gait-cycle detector (walk/run/sprint vs idle) at the
// loco-clip-change branch in PoseSampler::update. Returns 0 if the
// hip joint isn't in `s` or the animation is null/zero-duration.
float computeHipXZPathLength(const PoseSampler::Impl& s, const ozz::animation::Animation* anim)
{
    if (!anim || s.hips_joint_idx < 0)
        return 0.0f;
    const float dur = anim->duration();
    if (dur <= 0.0f)
        return 0.0f;
    const ozz::animation::Skeleton& skel = *s.skeleton;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);
    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &s.root_transform, sizeof(glm::mat4));
    constexpr float step = 1.0f / 60.0f;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;
    float total = 0.0f;
    glm::vec2 prev(0.0f);
    bool have_prev = false;
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
        sjob.context = &ctx;
        sjob.ratio = t / dur;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return total;
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return total;
        const ozz::math::Float4x4& m = models[s.hips_joint_idx];
        alignas(16) float col3[4];
        ozz::math::StorePtr(m.cols[3], col3);
        const glm::vec2 cur(col3[0], col3[2]);
        if (have_prev)
            total += glm::length(cur - prev);
        prev = cur;
        have_prev = true;
    }
    return total;
}

// Read live world position of joint `idx` from the impl's most-recent
// model_matrices. Returns true on success, false if idx is invalid.
bool readLiveJointWorldPos(const PoseSampler::Impl& s, int idx, glm::vec3& out)
{
    if (idx < 0 || idx >= static_cast<int>(s.model_matrices.size()))
        return false;
    glm::mat4 m;
    std::memcpy(&m, &s.model_matrices[idx], sizeof(glm::mat4));
    out = glm::vec3(m[3]);
    return true;
}

// Scan an animation for the time at which `joints` (indexed in
// skel) most closely match `ref` world positions, sampled at 60Hz.
// Returns 0 on bad inputs.
float scanLocoForBestPoseMatch(const ozz::animation::Animation& anim,
                               const ozz::animation::Skeleton& skel,
                               const ozz::math::Float4x4& root_storage,
                               const std::vector<int>& joints, const std::vector<glm::vec3>& ref)
{
    const float dur = anim.duration();
    if (dur <= 0.0f)
        return 0.0f;
    const int n_joints_skel = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints_skel);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints_skel);
    const float step = 1.0f / 60.0f;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;
    float best_t = 0.0f;
    float best_d2 = std::numeric_limits<float>::infinity();
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        ozz::animation::SamplingJob sjob;
        sjob.animation = &anim;
        sjob.context = &ctx;
        sjob.ratio = std::clamp(t / dur, 0.0f, 1.0f);
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            continue;
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            continue;
        float d2 = 0.0f;
        for (std::size_t k = 0; k < joints.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joints[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            const glm::vec3 cand(col3[0], col3[1], col3[2]);
            const glm::vec3 d = cand - ref[k];
            d2 += glm::dot(d, d);
        }
        if (d2 < best_d2)
        {
            best_d2 = d2;
            best_t = t;
        }
    }
    return best_t;
}

void applyBlendOutLocoPoseMatch(PoseSampler::Impl& s)
{
    if (s.loco_current.animation == nullptr)
        return;
    const char* names[] = {"mixamorig:LeftUpLeg", "mixamorig:RightUpLeg", "mixamorig:LeftFoot",
                           "mixamorig:RightFoot"};
    std::vector<int> joints;
    std::vector<glm::vec3> ref;
    for (const char* n : names)
    {
        const int idx = findSkeletonJoint(*s.skeleton, n);
        glm::vec3 pos;
        if (readLiveJointWorldPos(s, idx, pos))
        {
            joints.push_back(idx);
            ref.push_back(pos);
        }
    }
    if (joints.empty())
        return;
    ozz::math::Float4x4 root_storage;
    std::memcpy(&root_storage, &s.root_transform, sizeof(glm::mat4));
    const float best_t =
        scanLocoForBestPoseMatch(*s.loco_current.animation, *s.skeleton, root_storage, joints, ref);
    s.loco_current.time_seconds = best_t;
    s.loco_current.last_hip_xz_valid = false;
    s.loco_current.last_hip_delta = glm::vec3(0.0f);
}

// Advance one Track's clock by `dt`. Looping clips wrap; non-looping
// ones clamp at duration and set `finished`.
void advanceLocoTrack(Track& t, float dt)
{
    if (!t.animation)
        return;
    t.time_seconds += dt;
    const float dur = t.animation->duration();
    if (dur > 0.0f && t.time_seconds >= dur)
    {
        if (t.loops)
        {
            t.time_seconds = std::fmod(t.time_seconds, dur);
        }
        else
        {
            t.time_seconds = dur;
            t.finished = true;
        }
    }
}

// Advance a one-shot Track by `dt * rate`. Returns true when the
// clip reaches its last frame this tick. One-shots never loop.
bool advanceOneShotTrack(Track& t, float dt, float rate)
{
    if (!t.animation)
        return false;
    const float dur = t.animation->duration();
    t.time_seconds += dt * rate;
    if (dur > 0.0f && t.time_seconds >= dur)
    {
        t.time_seconds = dur;
        return true;
    }
    return false;
}

// True when the active one-shot is full-mask AND in Hold phase. In
// this state the loco track is fully occluded by the one-shot
// (loco_weight = max(0, 1 - one_shot_weight) = 0), so any state
// change to loco_current.animation while frozen would be invisible
// in the moment but stages bad data for the upcoming BlendOut
// handoff: applyBlendOutLocoPoseMatch scans loco_current.animation
// for the closest pose-match to the one-shot's exit pose, and a clip
// silently swapped during Hold is rarely geometrically compatible
// (e.g. dodge exit pose vs running's t=0 region produces 0.8m+ foot
// residuals → visible spasm at BlendOut). Gate is checked at every
// loco-state mutation site; see leg-spasm memory.
bool isLocoFrozenByFullMaskHold(const PoseSampler::Impl& s)
{
    // Despite the legacy name, this predicate now covers BlendOut
    // too. Rationale: during a full-mask one-shot's BlendOut, the
    // loco track is BECOMING visible from being occluded; an SM
    // clip swap mid-BlendOut snaps the loco to a new clip's t=0
    // region, and the dodge fading out reveals that snapped pose
    // (visible leg spasm). Holding the loco clip stable through
    // both Hold and BlendOut means the dodge fades into the SAME
    // clip the player was on before the dodge — no surprise reveal.
    //
    // Per-profile opt-in via one_shot_freeze_loco: dodges/blocks
    // set true (no stance change intended); attacks set false (the
    // attack TRIGGERS a stance change, so combat-idle should be
    // live by BlendOut for a clean reveal — freezing here would
    // show standard_idle for a frame after the attack ends, then
    // crossfade to combat-idle, which reads as "regular idle then
    // combat idle" instead of "straight to combat idle").
    const bool fading_or_held =
        s.one_shot_phase == OneShotPhase::Hold || s.one_shot_phase == OneShotPhase::BlendOut;
    return fading_or_held && s.one_shot_mask == PoseSampler::BodyMask::Full &&
           s.one_shot_freeze_loco;
}

// Drive every active track's clock forward and ramp the previous
// one-shot's fade-out weight. Returns whether the active one-shot
// just hit its last frame (signal for the phase machine).
bool advanceAllTracks(PoseSampler::Impl& s, float dt)
{
    advanceLocoTrack(s.loco_current, dt);
    advanceLocoTrack(s.loco_previous, dt);
    const bool one_shot_finished = advanceOneShotTrack(s.one_shot, dt, s.one_shot_playback_rate);
    if (s.one_shot_previous.animation != nullptr)
    {
        advanceOneShotTrack(s.one_shot_previous, dt, s.one_shot_previous_playback_rate);
        if (s.one_shot_previous_fade_seconds > 0.0f)
        {
            s.one_shot_previous_weight =
                std::max(0.0f, s.one_shot_previous_weight - dt / s.one_shot_previous_fade_seconds);
        }
        if (s.one_shot_previous_weight <= 0.0f)
        {
            s.one_shot_previous.animation = nullptr;
            s.one_shot_previous.registry_key.clear();
        }
    }
    return one_shot_finished;
}

// Tick the locomotion crossfade clock. When elapsed reaches duration,
// the crossfade completes: weight pins to 1, previous track is
// dropped (with its time stashed for resume).
void tickLocoCrossfade(PoseSampler::Impl& s, float dt)
{
    if (s.loco_blend_duration <= 0.0f)
        return;
    s.loco_blend_elapsed += dt;
    if (s.loco_blend_elapsed >= s.loco_blend_duration)
    {
        s.loco_blend_weight = 1.0f;
        s.loco_blend_elapsed = 0.0f;
        s.loco_blend_duration = 0.0f;
        if (s.loco_previous.animation != nullptr)
            s.last_clip_time[s.loco_previous.animation] = s.loco_previous.time_seconds;
        s.loco_previous.animation = nullptr;
        s.loco_previous.registry_key.clear();
    }
    else
    {
        s.loco_blend_weight = s.loco_blend_elapsed / s.loco_blend_duration;
    }
}

// Drive the one-shot phase machine. `dt` advances ramps;
// `one_shot_finished` reports whether the active clip already hit its
// last frame this tick. Returns true if the phase JUST entered
// BlendOut (signal to do the loco pose-match snap).
// True if the active one-shot has reached its auto-blend-out window
// and should transition. Held actions (`freeze_last`) bypass auto-
// transition and wait for an explicit releaseOneShot() call.
bool oneShotShouldEnterBlendOut(const PoseSampler::Impl& s, float blend_out_clip_time)
{
    if (s.one_shot_freeze_last)
        return false;
    const float dur = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
    return dur > 0.0f && s.one_shot.time_seconds >= dur - blend_out_clip_time;
}

// BlendIn step: ramp weight up; promote to Hold once full.
void tickPhaseBlendIn(PoseSampler::Impl& s, float dt)
{
    if (s.one_shot_blend_in_seconds <= 0.0f)
    {
        s.one_shot_weight = 1.0f;
        s.one_shot_phase = OneShotPhase::Hold;
        return;
    }
    s.one_shot_weight = std::min(1.0f, s.one_shot_weight + dt / s.one_shot_blend_in_seconds);
    if (s.one_shot_weight >= 1.0f)
        s.one_shot_phase = OneShotPhase::Hold;
}

// BlendOut step: ramp weight down; deactivate once it hits zero.
void tickPhaseBlendOut(PoseSampler::Impl& s, float dt)
{
    if (s.one_shot_blend_out_seconds <= 0.0f)
    {
        s.one_shot_weight = 0.0f;
        s.one_shot_phase = OneShotPhase::Inactive;
        s.one_shot.animation = nullptr;
        s.one_shot.registry_key.clear();
        return;
    }
    s.one_shot_weight = std::max(0.0f, s.one_shot_weight - dt / s.one_shot_blend_out_seconds);
    if (s.one_shot_weight <= 0.0f)
    {
        s.one_shot_phase = OneShotPhase::Inactive;
        s.one_shot.animation = nullptr;
        s.one_shot.registry_key.clear();
    }
}

bool tickOneShotPhase(PoseSampler::Impl& s, float dt, bool one_shot_finished)
{
    const float blend_out_clip_time = s.one_shot_blend_out_seconds * s.one_shot_playback_rate;
    bool blend_out_entered = false;
    switch (s.one_shot_phase)
    {
    case OneShotPhase::Inactive:
        s.one_shot_weight = 0.0f;
        break;
    case OneShotPhase::BlendIn:
        tickPhaseBlendIn(s, dt);
        // BlendIn auto-advances only on the clip-end window, not on
        // one_shot_finished: a clip that finishes during BlendIn keeps
        // ramping weight (the auto-fade comes via Hold next tick).
        if (oneShotShouldEnterBlendOut(s, blend_out_clip_time))
        {
            s.one_shot_phase = OneShotPhase::BlendOut;
            blend_out_entered = true;
        }
        break;
    case OneShotPhase::Hold:
        if (!s.one_shot_freeze_last &&
            (one_shot_finished || oneShotShouldEnterBlendOut(s, blend_out_clip_time)))
        {
            s.one_shot_phase = OneShotPhase::BlendOut;
            blend_out_entered = true;
        }
        break;
    case OneShotPhase::BlendOut:
        tickPhaseBlendOut(s, dt);
        break;
    }
    return blend_out_entered;
}

// Per-track hip-XZ delta extract: read the track's hip translation
// in local space and compute a per-frame delta. Stationary clips
// always emit zero. Discontinuities (>0.5m/frame) are squelched and
// re-baselined.
void extractTrackHipDelta(Track& t, int hip_soa, int hip_lane, float gait_threshold)
{
    if (!t.animation)
    {
        t.resetHipTracking();
        return;
    }
    const ozz::math::SoaTransform& T = t.local_transforms[hip_soa];
    alignas(16) float tx[4];
    alignas(16) float tz[4];
    ozz::math::StorePtr(T.translation.x, tx);
    ozz::math::StorePtr(T.translation.z, tz);
    const glm::vec2 cur(tx[hip_lane], tz[hip_lane]);
    const bool stationary = t.hip_path_cached >= 0.0f && t.hip_path_cached < gait_threshold;
    if (stationary)
    {
        t.last_hip_delta = glm::vec3(0.0f);
        t.last_hip_xz = cur;
        t.last_hip_xz_valid = true;
        return;
    }
    if (t.last_hip_xz_valid)
    {
        const glm::vec2 d = cur - t.last_hip_xz;
        const bool discontinuity = std::abs(d.x) > 0.5f || std::abs(d.y) > 0.5f;
        t.last_hip_delta = discontinuity ? glm::vec3(0.0f) : glm::vec3(d.x, 0.0f, d.y);
    }
    else
    {
        t.last_hip_delta = glm::vec3(0.0f);
    }
    t.last_hip_xz = cur;
    t.last_hip_xz_valid = true;
}

// Run the per-track hip-XZ delta extract for all motion-bearing
// tracks. No-op when the skeleton has no hips joint.
void extractAllHipDeltas(PoseSampler::Impl& s)
{
    if (s.hips_joint_idx < 0)
        return;
    ZoneScopedN("hip-delta-extract");
    constexpr float kGaitCycleThreshold = 0.5f;
    const int hip_soa = s.hips_joint_idx / 4;
    const int hip_lane = s.hips_joint_idx % 4;
    extractTrackHipDelta(s.loco_current, hip_soa, hip_lane, kGaitCycleThreshold);
    extractTrackHipDelta(s.loco_previous, hip_soa, hip_lane, kGaitCycleThreshold);
    extractTrackHipDelta(s.one_shot, hip_soa, hip_lane, kGaitCycleThreshold);
}

// Two-layer locomotion blend (current + previous). Output goes to
// `s.loco_blended`. Returns false on ozz job failure.
bool runLocoBlend(PoseSampler::Impl& s)
{
    ZoneScopedN("blend-loco");
    ozz::animation::BlendingJob::Layer loco_layers[2];
    loco_layers[0].weight = s.loco_blend_weight;
    loco_layers[0].transform = ozz::make_span(s.loco_current.local_transforms);
    loco_layers[1].weight =
        (1.0f - s.loco_blend_weight) * (s.loco_previous.animation ? 1.0f : 0.0f);
    loco_layers[1].transform = ozz::make_span(s.loco_previous.local_transforms);

    ozz::animation::BlendingJob loco_blend;
    loco_blend.layers = ozz::span<const ozz::animation::BlendingJob::Layer>(loco_layers, 2);
    loco_blend.rest_pose = s.skeleton->joint_rest_poses();
    loco_blend.output = ozz::make_span(s.loco_blended);
    return loco_blend.Run();
}

// Pack a per-joint SoA weight buffer for one one-shot track. If the
// track is upper-body-masked and weights are available, scale the
// per-joint upper-body weights by the track's blend weight; otherwise
// fill all lanes with the track weight.
void packOneShotJointWeights(std::vector<ozz::math::SimdFloat4>& out,
                             const std::vector<ozz::math::SimdFloat4>& upper_body_weights,
                             PoseSampler::BodyMask mask, float weight)
{
    const std::size_t n_soa = out.size();
    const ozz::math::SimdFloat4 w_simd = ozz::math::simd_float4::Load1(weight);
    if (mask == PoseSampler::BodyMask::UpperBody && !upper_body_weights.empty())
    {
        for (std::size_t i = 0; i < n_soa; ++i)
            out[i] = upper_body_weights[i] * w_simd;
    }
    else
    {
        for (std::size_t i = 0; i < n_soa; ++i)
            out[i] = w_simd;
    }
}

// Three-layer final blend: loco vs current one-shot vs previous
// one-shot. Fast path: if both one-shots are inactive, just copy
// loco_blended → final_locals. Returns false on ozz job failure.
bool runFinalBlend(PoseSampler::Impl& s)
{
    if (s.one_shot.animation == nullptr && s.one_shot_previous.animation == nullptr)
    {
        ZoneScopedN("blend-final-fast");
        s.final_locals = s.loco_blended;
        return true;
    }
    ZoneScopedN("blend-final");
    packOneShotJointWeights(s.one_shot_joint_weights, s.upper_body_weights, s.one_shot_mask,
                            s.one_shot_weight);
    packOneShotJointWeights(s.one_shot_previous_joint_weights, s.upper_body_weights,
                            s.one_shot_previous_mask, s.one_shot_previous_weight);

    // Loco layer ramps down by the SUM of full-mask one-shot weights.
    // Upper-body masks don't reduce loco (legs need pure loco).
    const float full_active_w =
        (s.one_shot_mask == PoseSampler::BodyMask::Full) ? s.one_shot_weight : 0.0f;
    const float full_prev_w = (s.one_shot_previous_mask == PoseSampler::BodyMask::Full)
                                  ? s.one_shot_previous_weight
                                  : 0.0f;
    const float loco_weight = std::max(0.0f, 1.0f - full_active_w - full_prev_w);

    ozz::animation::BlendingJob::Layer final_layers[3];
    int n_layers = 1;
    final_layers[0].weight = loco_weight;
    final_layers[0].transform = ozz::make_span(s.loco_blended);
    if (s.one_shot_previous.animation != nullptr && s.one_shot_previous_weight > 0.0f)
    {
        final_layers[n_layers].weight = 1.0f;
        final_layers[n_layers].transform = ozz::make_span(s.one_shot_previous.local_transforms);
        final_layers[n_layers].joint_weights = ozz::make_span(s.one_shot_previous_joint_weights);
        ++n_layers;
    }
    if (s.one_shot.animation != nullptr)
    {
        final_layers[n_layers].weight = 1.0f;
        final_layers[n_layers].transform = ozz::make_span(s.one_shot.local_transforms);
        final_layers[n_layers].joint_weights = ozz::make_span(s.one_shot_joint_weights);
        ++n_layers;
    }
    ozz::animation::BlendingJob final_blend;
    final_blend.layers =
        ozz::span<const ozz::animation::BlendingJob::Layer>(final_layers, n_layers);
    final_blend.rest_pose = s.skeleton->joint_rest_poses();
    final_blend.output = ozz::make_span(s.final_locals);
    return final_blend.Run();
}

// Pin the visible hip XZ to the rest-pose translation so the
// character body doesn't drift relative to its world transform. Y
// (vertical bob) is preserved.
void freezeVisualHipXZ(PoseSampler::Impl& s)
{
    if (s.hips_joint_idx < 0)
        return;
    const int hip_soa = s.hips_joint_idx / 4;
    const int hip_lane = s.hips_joint_idx % 4;
    ozz::math::SoaTransform& T = s.final_locals[hip_soa];
    alignas(16) float tx[4];
    alignas(16) float tz[4];
    ozz::math::StorePtr(T.translation.x, tx);
    ozz::math::StorePtr(T.translation.z, tz);
    alignas(16) float rest[4];
    ozz::math::StorePtr(s.hips_rest_translation, rest);
    tx[hip_lane] = rest[0];
    tz[hip_lane] = rest[2];
    T.translation.x = ozz::math::simd_float4::Load(tx[0], tx[1], tx[2], tx[3]);
    T.translation.z = ozz::math::simd_float4::Load(tz[0], tz[1], tz[2], tz[3]);
}

// Run the local-to-model job, producing world-space bone matrices.
// Returns false on ozz job failure.
bool runLocalToModel(PoseSampler::Impl& s)
{
    ZoneScopedN("local-to-model");
    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &s.root_transform, sizeof(glm::mat4));
    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = s.skeleton;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(s.final_locals);
    ljob.output = ozz::make_span(s.model_matrices);
    return ljob.Run();
}

// Build the GPU skinning palette from the per-bone model matrices
// and the inverse bind matrices.
void computeGpuPalette(const PoseSampler::Impl& s, std::vector<glm::mat4>& bone_palette)
{
    ZoneScopedN("gpu-palette");
    const std::size_t bone_count = s.model_matrices.size();
    for (std::size_t i = 0; i < bone_count; ++i)
    {
        glm::mat4 current_model;
        std::memcpy(&current_model, &s.model_matrices[i], sizeof(glm::mat4));
        const glm::mat4 inv_bind =
            (i < s.inverse_bind_matrices.size()) ? s.inverse_bind_matrices[i] : glm::mat4(1.0f);
        bone_palette[i] = current_model * inv_bind;
    }
}

} // namespace

bool PoseSampler::update(const AnimationClip& clip, float dt, float blend_seconds, bool loops,
                         const char* clip_key)
{
    ZoneScopedN("PoseSampler::update");
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return false;

    Impl& s = *impl;
    const ozz::animation::Animation* desired = clip.ozz_animation.get();
    const std::string desired_key = (clip_key != nullptr) ? clip_key : "";

    const bool capture_this_frame = consumeInertializationCaptureFlag(s);

    // ---- Locomotion clip change → start a crossfade ----
    if (desired != s.loco_current.animation)
        routeLocoClipChange(*this, s, clip, desired, desired_key, blend_seconds);
    else
        s.last_frozen_swap_attempt.clear();
    // Caller-supplied loops flag applies to the loco_current track —
    // whether or not we just rebound it. Stable through the lifetime
    // of the clip-change in flight.
    s.loco_current.loops = loops;

    // ---- Advance all clocks (loco + one-shot + previous one-shot) ----
    const bool one_shot_finished = advanceAllTracks(s, dt);

    // ---- Advance locomotion crossfade ----
    tickLocoCrossfade(s, dt);

    // ---- Advance one-shot phase machine; if it just entered BlendOut
    //      this frame, snap loco time to the closest pose-match frame
    //      so the crossfade reveals a near-identical loco pose. ----
    if (tickOneShotPhase(s, dt, one_shot_finished))
        applyBlendOutLocoPoseMatch(s);

    // ---- Sample all four tracks ----
    {
        ZoneScopedN("sample-tracks");
        s.loco_current.sample();
        s.loco_previous.sample();
        s.one_shot.sample();
        if (s.one_shot_previous.animation != nullptr)
            s.one_shot_previous.sample();
    }

    // ---- Per-track hip XZ delta (root-motion source) ----
    // Velocity-level blend: each track contributes its own clip-
    // authored delta, summed by consumedHipDelta(). Pose-level blend
    // would create phantom motion when crossfading between clips at
    // different hip positions.
    extractAllHipDeltas(s);

    // ---- Two-stage blend: loco crossfade, then loco vs one-shots ----
    if (!runLocoBlend(s))
        return false;
    if (!runFinalBlend(s))
        return false;

    // ---- Inertialization step 2 ----
    if (capture_this_frame)
        captureInertializationOffset(s);
    if (s.decay_active && s.decay_max_seconds > 0.0f)
        applyInertializationDecay(s, dt);

    // ---- Visual hip XZ freeze (anchor body to model origin) ----
    freezeVisualHipXZ(s);

    // ---- Local-to-model + GPU palette ----
    if (!runLocalToModel(s))
        return false;
    computeGpuPalette(s, bone_palette);

    // Save post-blend pose for next frame's potential inertialization
    // capture. This is what was actually rendered (after blend, after
    // hip-freeze, after any decay applied this frame).
    s.post_locals_last = s.final_locals;
    return true;
}

} // namespace selva::anim
