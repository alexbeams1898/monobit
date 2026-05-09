#include "anim/PoseSampler.h"

#include "anim/AnimationClip.h"
#include "anim/SkeletalMesh.h"
#include "anim/Skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>
#include <tracy/Tracy.hpp>

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
}

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
static std::vector<ozz::math::SimdFloat4>
buildUpperBodyWeights(const ozz::animation::Skeleton& skel)
{
    const int n = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    const auto names = skel.joint_names();
    const auto parents = skel.joint_parents();

    auto findJoint = [&](const char* name) -> int
    {
        for (int i = 0; i < n; ++i)
            if (names[i] != nullptr && std::strcmp(names[i], name) == 0)
                return i;
        return -1;
    };

    const int left_leg = findJoint("mixamorig:LeftUpLeg");
    const int right_leg = findJoint("mixamorig:RightUpLeg");

    std::vector<float> per_joint(n, 1.0f);
    std::vector<bool> in_leg(n, false);
    if (left_leg >= 0)
        in_leg[left_leg] = true;
    if (right_leg >= 0)
        in_leg[right_leg] = true;

    // ozz guarantees parents come before children in the joint array —
    // single forward pass propagates the leg-subtree flag down.
    for (int i = 0; i < n; ++i)
    {
        if (parents[i] >= 0 && in_leg[parents[i]])
            in_leg[i] = true;
        if (in_leg[i])
            per_joint[i] = 0.0f;
    }

    // Diagnostic summary — runs once per character at construction.
    int n_upper = 0;
    int n_lower = 0;
    for (int i = 0; i < n; ++i)
        (per_joint[i] > 0.5f ? n_upper : n_lower) += 1;
    std::fprintf(stderr,
                 "[PoseSampler] upper-body mask: %d upper / %d lower (LeftUpLeg=%d, "
                 "RightUpLeg=%d)\n",
                 n_upper, n_lower, left_leg, right_leg);

    // Pack per-joint floats into SoA-aligned SimdFloat4 lanes.
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
                              float playback_rate, bool freeze_last)
{
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return;

    Impl& s = *impl;
    s.one_shot.animation = clip.ozz_animation.get();
    const float dur = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
    s.one_shot.time_seconds = std::clamp(start_time_seconds, 0.0f, std::max(0.0f, dur - 1e-4f));
    s.one_shot_phase = OneShotPhase::BlendIn;
    s.one_shot_weight = 0.0f;
    s.one_shot_blend_in_seconds = std::max(0.0f, blend_in_seconds);
    s.one_shot_blend_out_seconds = std::max(0.0f, blend_out_seconds);
    s.one_shot_playback_rate = std::clamp(playback_rate, 0.1f, 10.0f);
    s.one_shot_mask = mask;
    s.one_shot_freeze_last = freeze_last;
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

    const ozz::animation::Skeleton& skel = *impl->skeleton;
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();

    // Scratch state for a one-off sample sweep. Mirrors what the runtime
    // tracks own; we don't reuse those because they hold the live one-
    // shot/locomotion state and we don't want to perturb it.
    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;

    auto sample_hip_xz = [&](float t, glm::vec2& out) -> bool
    {
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
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
        const ozz::math::Float4x4& m = models[impl->hips_joint_idx];
        alignas(16) float col3[4];
        ozz::math::StorePtr(m.cols[3], col3);
        out = glm::vec2(col3[0], col3[2]);
        return true;
    };

    // Pass 1: gather per-step hip XZ positions, find peak per-step
    // displacement (proxy for peak hip velocity).
    std::vector<glm::vec2> samples;
    samples.reserve(static_cast<std::size_t>(n_steps));
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        glm::vec2 p;
        if (!sample_hip_xz(t, p))
            return result;
        samples.push_back(p);
    }
    if (samples.size() < 2)
        return result;

    float peak_step = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
    {
        const float d = glm::length(samples[i] - samples[i - 1]);
        if (d > peak_step)
            peak_step = d;
    }

    // Pass 2: integrate path length up to the first sample whose
    // per-step displacement drops below quiet_velocity_fraction × peak.
    // We only consider sub-threshold steps AFTER the peak has occurred,
    // so the slow blend-in at the start of a clip doesn't terminate
    // the integration prematurely.
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
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    for (int j : joint_indices)
        if (j < 0 || j >= n_joints)
            return dur; // invalid index -> safe fallback

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;

    // Per-joint XYZ samples across the clip.
    std::vector<std::vector<glm::vec3>> joint_samples(joint_indices.size());
    for (auto& v : joint_samples)
        v.reserve(static_cast<std::size_t>(n_steps));

    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
        sjob.context = &ctx;
        sjob.ratio = ratio;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return dur;
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return dur;
        for (std::size_t k = 0; k < joint_indices.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joint_indices[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            joint_samples[k].emplace_back(col3[0], col3[1], col3[2]);
        }
    }

    // Per-joint motion-end time. The watched-joint set's settle time
    // is the MAX across joints — every watched joint must have stilled.
    float settle_time = 0.0f;
    for (const auto& samples : joint_samples)
    {
        if (samples.size() < 2)
            continue;
        float peak_step = 0.0f;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const float d = glm::length(samples[i] - samples[i - 1]);
            if (d > peak_step)
                peak_step = d;
        }
        const float quiet_threshold = peak_step * std::max(0.0f, quiet_velocity_fraction);
        bool seen_peak = false;
        float motion_end_t = dur;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const float d = glm::length(samples[i] - samples[i - 1]);
            if (d >= peak_step * 0.9f)
                seen_peak = true;
            // Threshold meaning: motion-end fires when velocity has
            // dropped to `quiet_velocity_fraction` of peak AFTER the
            // peak has occurred. With fraction ~0.5, that's mid-
            // follow-through (hand still moving, past contact, hasn't
            // settled to stance). With fraction ~0.1, that's full
            // settle (hand at rest at stance). Mid-follow-through is
            // the better cancel point for chain transitions because
            // the source pose for the next swing's blend is still in
            // motion in approximately the right direction — small
            // pose gap, aligned velocity.
            if (seen_peak && d < quiet_threshold)
            {
                motion_end_t = static_cast<float>(i) * step;
                break;
            }
        }
        if (motion_end_t > settle_time)
            settle_time = motion_end_t;
    }
    return std::min(settle_time, dur);
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
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    for (int j : joint_indices)
        if (j < 0 || j >= n_joints)
            return 0.0f;

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;

    std::vector<std::vector<glm::vec3>> joint_samples(joint_indices.size());
    for (auto& v : joint_samples)
        v.reserve(static_cast<std::size_t>(n_steps));

    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
        sjob.context = &ctx;
        sjob.ratio = ratio;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return 0.0f;
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return 0.0f;
        for (std::size_t k = 0; k < joint_indices.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joint_indices[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            joint_samples[k].emplace_back(col3[0], col3[1], col3[2]);
        }
    }

    // Earliest moment ANY watched joint enters its active phase.
    // (MIN, not MAX — chain link should fire as soon as the first
    // joint starts contributing to the swing.)
    float earliest_start = dur;
    for (const auto& samples : joint_samples)
    {
        if (samples.size() < 2)
            continue;
        float peak_step = 0.0f;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const float d = glm::length(samples[i] - samples[i - 1]);
            if (d > peak_step)
                peak_step = d;
        }
        const float start_threshold = peak_step * std::max(0.0f, start_velocity_fraction);
        float motion_start_t = 0.0f;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const float d = glm::length(samples[i] - samples[i - 1]);
            if (d >= start_threshold)
            {
                motion_start_t = static_cast<float>(i) * step;
                break;
            }
        }
        if (motion_start_t < earliest_start)
            earliest_start = motion_start_t;
    }
    return std::min(earliest_start, dur);
}

float PoseSampler::clipJointMotionPeak(const AnimationClip& clip,
                                       const std::vector<int>& joint_indices,
                                       float sample_hz) const
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
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    for (int j : joint_indices)
        if (j < 0 || j >= n_joints)
            return 0.5f * dur;

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil(dur / step)) + 1;

    std::vector<std::vector<glm::vec3>> joint_samples(joint_indices.size());
    for (auto& v : joint_samples)
        v.reserve(static_cast<std::size_t>(n_steps));

    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(static_cast<float>(i) * step, dur);
        const float ratio = t / dur;
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
        sjob.context = &ctx;
        sjob.ratio = ratio;
        sjob.output = ozz::make_span(locals);
        if (!sjob.Run())
            return 0.5f * dur;
        ozz::animation::LocalToModelJob ljob;
        ljob.skeleton = &skel;
        ljob.root = &root_storage;
        ljob.input = ozz::make_span(locals);
        ljob.output = ozz::make_span(models);
        if (!ljob.Run())
            return 0.5f * dur;
        for (std::size_t k = 0; k < joint_indices.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joint_indices[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            joint_samples[k].emplace_back(col3[0], col3[1], col3[2]);
        }
    }

    // Per-joint argmax velocity (step distance). Latest peak across
    // joints is the splice cutoff — past this, all watched joints
    // are decelerating into recovery.
    float latest_peak = 0.0f;
    for (const auto& samples : joint_samples)
    {
        if (samples.size() < 2)
            continue;
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
        if (peak_t > latest_peak)
            latest_peak = peak_t;
    }
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
    const int n_joints = skel.num_joints();
    const int n_soa = skel.num_soa_joints();
    for (int j : joint_indices)
        if (j < 0 || j >= n_joints)
            return 0.0f;

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(n_joints);
    std::vector<ozz::math::SoaTransform> locals(n_soa);
    std::vector<ozz::math::Float4x4> models(n_joints);

    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    auto sample_joint_positions =
        [&](const ozz::animation::Animation* anim, float t, float dur,
            std::vector<glm::vec3>& out) -> bool
    {
        const float ratio = std::clamp(t / dur, 0.0f, 1.0f);
        ozz::animation::SamplingJob sjob;
        sjob.animation = anim;
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
        out.resize(joint_indices.size());
        for (std::size_t k = 0; k < joint_indices.size(); ++k)
        {
            const ozz::math::Float4x4& m = models[joint_indices[k]];
            alignas(16) float col3[4];
            ozz::math::StorePtr(m.cols[3], col3);
            out[k] = glm::vec3(col3[0], col3[1], col3[2]);
        }
        return true;
    };

    // Reference pose: prev clip at prev_t_seconds.
    std::vector<glm::vec3> ref;
    if (!sample_joint_positions(prev_anim, prev_t_seconds, prev_anim->duration(), ref))
        return 0.0f;

    // Sweep next clip; find argmin distance.
    const float t_start = std::max(0.0f, search_window_start);
    const float t_end =
        (search_window_end > 0.0f) ? std::min(search_window_end, next_dur) : next_dur;
    if (t_end <= t_start)
        return t_start;
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil((t_end - t_start) / step)) + 1;

    std::vector<glm::vec3> candidate;
    float best_t = t_start;
    float best_dist_sq = std::numeric_limits<float>::infinity();
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(t_start + static_cast<float>(i) * step, t_end);
        if (!sample_joint_positions(next_anim, t, next_dur, candidate))
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
    for (int j : joint_indices)
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
    const float t_end =
        (search_window_end > 0.0f) ? std::min(search_window_end, dur) : dur;
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
    d.loco_current_name = s.loco_current.animation ? s.loco_current.animation->name() : nullptr;
    d.loco_previous_name = s.loco_previous.animation ? s.loco_previous.animation->name() : nullptr;
    d.one_shot_name = s.one_shot.animation ? s.one_shot.animation->name() : nullptr;
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

bool PoseSampler::update(const AnimationClip& clip, float dt, float blend_seconds, bool loops)
{
    ZoneScopedN("PoseSampler::update");
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return false;

    Impl& s = *impl;
    const ozz::animation::Animation* desired = clip.ozz_animation.get();

    // Inertialization step 1 — capture. Two modes:
    //   * Default: snapshot the previous frame's rendered pose
    //     (post_locals_last) into pre_change_locals. After the blend,
    //     compute offset = source - new_pose. Right for locomotion.
    //   * From-supplied: pre_change_locals is already the canonical
    //     source pose (set by requestInertializationFromPose). Skip
    //     the snapshot — go straight to offset compute.
    bool capture_this_frame = false;
    if (s.pending_capture)
    {
        if (!s.pending_capture_from_supplied_source)
            s.pre_change_locals = s.post_locals_last;
        capture_this_frame = true;
        s.pending_capture = false;
        s.pending_capture_from_supplied_source = false;
    }

    // ---- Locomotion clip change → start a crossfade ----
    if (desired != s.loco_current.animation)
    {
        // Stash the outgoing clip's time so a future re-entry can
        // resume from here rather than snapping to t=0.
        if (s.loco_current.animation != nullptr)
            s.last_clip_time[s.loco_current.animation] = s.loco_current.time_seconds;
        // Phase-match the new clip's start time to the previous clip's
        // normalized cycle position. Without this, walk→run starts the
        // run clip at t=0 regardless of where walk was in its cycle —
        // the two clips' hip motions are at uncoordinated cycle phases
        // during the crossfade and their blended hip position can move
        // backward for one frame. Phase matching aligns the legs: walk
        // at 60% of its cycle is "left foot down, right foot swinging"
        // — running at 60% should start there too. Unity Animator
        // "Cycle Offset", Unreal "Sync Markers" do the same.
        //
        // Phase matching only applies between **gait-cycle clips**
        // (walk, run, sprint — where both clips have meaningful hip
        // XZ travel). For transitions involving an idle (or any clip
        // with no real gait cycle), the new clip starts at t=0
        // because there's no meaningful phase to map onto. Otherwise
        // run→idle would map run's gait phase onto idle's slow sway,
        // and the blended hip can stutter backward for a frame.
        //
        // Threshold of 0.5m hip path length is well below any walking
        // cycle (≥1.5m typical) and well above any idle sway (<0.2m
        // typical). Cached per-Track so the path-length scan happens
        // at most once per (Track, animation) pair.
        constexpr float kGaitCycleThreshold = 0.5f;

        // Compute path length for the new clip (lazily; cache in
        // a temporary because we haven't bound `desired` to a Track yet).
        // Reuses the existing clipHipPathLength scan logic via a
        // synthesized AnimationClip wrapper — except clipHipPathLength
        // takes an AnimationClip (which owns a unique_ptr to the
        // ozz Animation), not a raw ozz::Animation*. We'd have to
        // either refactor or duplicate. Going with duplicate-here for
        // simplicity: a small inline helper that runs the same scan
        // on a raw pointer.
        auto computeHipPath = [&](const ozz::animation::Animation* anim) -> float
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
                const float ratio = t / dur;
                ozz::animation::SamplingJob sjob;
                sjob.animation = anim;
                sjob.context = &ctx;
                sjob.ratio = ratio;
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
        };

        // Use cache if we already scanned this animation; otherwise
        // scan and store. Only the loco_current track needs caching
        // here because we don't crossfade out of a stale loco_previous.
        if (s.loco_current.hip_path_cached < 0.0f && s.loco_current.animation != nullptr)
            s.loco_current.hip_path_cached = computeHipPath(s.loco_current.animation);
        const float new_clip_path = computeHipPath(desired);

        const bool prev_is_gait = s.loco_current.animation != nullptr &&
                                  s.loco_current.hip_path_cached >= kGaitCycleThreshold;
        const bool new_is_gait = new_clip_path >= kGaitCycleThreshold;

        float new_start_time = 0.0f;
        const float prev_dur =
            s.loco_current.animation ? s.loco_current.animation->duration() : 0.0f;
        const float new_dur = desired->duration();
        if (prev_is_gait && new_is_gait && prev_dur > 0.0f && new_dur > 0.0f)
        {
            const float phase = std::fmod(s.loco_current.time_seconds, prev_dur) / prev_dur;
            new_start_time = phase * new_dur;
        }
        else
        {
            // Non-gait re-entry: resume the clip from where it last
            // left off. Otherwise rapid intent toggles (e.g. tapping
            // WASD in combat stance) snap unarmed_combat_idle's time
            // back to 0 every time, restarting its 3-second bounce
            // cycle from a different leg pose than what was on screen
            // a frame ago — visible leg spasm.
            const auto it = s.last_clip_time.find(desired);
            if (it != s.last_clip_time.end())
            {
                new_start_time = std::fmod(it->second, std::max(new_dur, 1e-4f));
                samplerDiagLog("[loco] resume %s @ cached t=%.3fs (prev=%s, new_dur=%.2fs)\n",
                               desired->name(), new_start_time,
                               s.loco_current.animation ? s.loco_current.animation->name()
                                                        : "(none)",
                               new_dur);
            }
            else
            {
                samplerDiagLog("[loco] cold-enter %s @ t=0 (no cache; prev=%s)\n",
                               desired->name(),
                               s.loco_current.animation ? s.loco_current.animation->name()
                                                        : "(none)");
            }
        }

        if (blend_seconds > 0.0f && s.loco_current.animation != nullptr)
        {
            // Reverse-blend special case: if `desired` is the clip we
            // were JUST blending FROM (loco_previous), don't restart
            // the crossfade — reverse it. Rapid WASD tapping in combat
            // stance produces this exact ping-pong: idle → walking →
            // idle → walking. Without this branch, each clip change
            // swaps tracks, snaps the new current's time to 0 (or
            // phase-matched, but idle isn't gait so it's 0), and
            // resets weight to 0. unarmed_combat_idle is a 3-second
            // bouncing loop — re-snapping its time to 0 every other
            // frame creates a visible leg spasm because the bounce
            // pose snaps to a different leg position each tap.
            //
            // Reverse-blend: we ARE the previous; previous becomes us.
            // Swap, keep both tracks' time_seconds intact, and flip
            // the weight so the in-flight blend now drives back toward
            // the formerly-previous-now-current clip.
            const bool reverse_blend = (s.loco_previous.animation == desired);
            if (reverse_blend)
            {
                samplerDiagLog("[loco] reverse-blend %s <-> %s (weight %.2f -> %.2f, t=%.3fs)\n",
                               s.loco_current.animation->name(), desired->name(),
                               s.loco_blend_weight, 1.0f - s.loco_blend_weight,
                               s.loco_previous.time_seconds);
                std::swap(s.loco_current.animation, s.loco_previous.animation);
                std::swap(s.loco_current.time_seconds, s.loco_previous.time_seconds);
                std::swap(s.loco_current.hip_path_cached, s.loco_previous.hip_path_cached);
                s.loco_current.local_transforms.swap(s.loco_previous.local_transforms);
                // Flip the weight: if we were 30% into idle→walking,
                // we're now 70% into walking→idle (the reverse path).
                const float old_weight = s.loco_blend_weight;
                s.loco_blend_weight = 1.0f - old_weight;
                s.loco_blend_elapsed = std::max(0.0f, s.loco_blend_duration - s.loco_blend_elapsed);
                // Don't override new_start_time below — keep the
                // resumed clip's existing time so the bounce/gait
                // doesn't visibly restart.
                s.loco_current.finished = false;
            }
            else
            {
                // Swap rather than move (Context is non-movable). Both
                // contexts were sized at createPoseSampler, so the swap
                // doesn't invalidate keyframe caches relative to either
                // track's animation pointer (each context belongs to its
                // physical Track and we swap which animation each is set
                // to). Swap the per-track hip_path cache along with
                // the animation so loco_previous keeps its old clip's
                // cached path length (needed when ANOTHER clip change
                // happens before the crossfade completes).
                std::swap(s.loco_current.animation, s.loco_previous.animation);
                std::swap(s.loco_current.time_seconds, s.loco_previous.time_seconds);
                std::swap(s.loco_current.hip_path_cached, s.loco_previous.hip_path_cached);
                s.loco_current.local_transforms.swap(s.loco_previous.local_transforms);
                s.loco_blend_weight = 0.0f;
                s.loco_blend_elapsed = 0.0f;
                s.loco_blend_duration = blend_seconds;
            }
            // Final rebind for the FORWARD-BLEND path only. Reverse-
            // blend already swapped in the right animation and wants
            // to preserve the resumed clip's existing time + hip
            // tracking — overwriting them would defeat the whole
            // point of reverse-blend.
            if (!reverse_blend)
            {
                s.loco_current.animation = desired;
                s.loco_current.time_seconds = new_start_time;
                s.loco_current.hip_path_cached = new_clip_path;
                s.loco_current.finished = false;
                // Discontinuity: the loco_current track just rebound to a new
                // clip. Force its next-frame delta to zero so we don't emit a
                // pose-snap delta from the previous clip's hip XZ to the new
                // clip's hip XZ. The loco_previous track keeps its tracking
                // (it's still playing the OLD clip during the crossfade).
                s.loco_current.resetHipTracking();
            }
            // Inertialization: capture previous-frame pose and decay any
            // residual offset over the blend-in window. Composes with the
            // crossfade — crossfade handles bulk pose interpolation,
            // inertialization smooths the leftover joint-velocity
            // discontinuity that crossfade alone can't address.
            s.pending_capture = true;
            s.decay_duration = std::max(0.0f, blend_seconds);
        }
        else
        {
            s.loco_previous.animation = nullptr;
            s.loco_previous.hip_path_cached = -1.0f;
            s.loco_blend_weight = 1.0f;
            s.loco_blend_elapsed = 0.0f;
            s.loco_blend_duration = 0.0f;
            s.loco_current.animation = desired;
            s.loco_current.time_seconds = new_start_time;
            s.loco_current.hip_path_cached = new_clip_path;
            s.loco_current.finished = false;
            s.loco_current.resetHipTracking();
            s.pending_capture = true;
            s.decay_duration = 0.0f;
        }
    }
    // Caller-supplied loops flag applies to the loco_current track —
    // whether or not we just rebound it. Stable through the lifetime
    // of the clip-change in flight.
    s.loco_current.loops = loops;

    // ---- Advance times on all three tracks ----
    // Locomotion tracks: looping clips wrap at their duration, non-
    // looping clips clamp at their last frame and set `finished` true.
    // The state machine uses `finished` to know when a transition clip
    // (start_walking, run_to_stop) has completed.
    auto advance_loco = [&](Track& t)
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
    };
    auto advance_one_shot = [&](Track& t, float rate) -> bool
    {
        // Returns true if the one-shot has reached its final frame.
        if (!t.animation)
            return false;
        const float dur = t.animation->duration();
        t.time_seconds += dt * rate;
        if (dur > 0.0f && t.time_seconds >= dur)
        {
            t.time_seconds = dur; // hold at last frame; do NOT loop
            return true;
        }
        return false;
    };
    advance_loco(s.loco_current);
    advance_loco(s.loco_previous);
    const bool one_shot_finished = advance_one_shot(s.one_shot, s.one_shot_playback_rate);

    // ---- Advance locomotion crossfade ----
    if (s.loco_blend_duration > 0.0f)
    {
        s.loco_blend_elapsed += dt;
        if (s.loco_blend_elapsed >= s.loco_blend_duration)
        {
            s.loco_blend_weight = 1.0f;
            s.loco_blend_elapsed = 0.0f;
            s.loco_blend_duration = 0.0f;
            // Stash the dropped clip's time so a later re-entry can
            // resume mid-bounce instead of snapping to 0.
            if (s.loco_previous.animation != nullptr)
                s.last_clip_time[s.loco_previous.animation] = s.loco_previous.time_seconds;
            s.loco_previous.animation = nullptr;
        }
        else
        {
            s.loco_blend_weight = s.loco_blend_elapsed / s.loco_blend_duration;
        }
    }

    // ---- Advance one-shot phase machine ----
    // The auto-blend-out kicks off when the one-shot's remaining time
    // drops below blend_out_seconds (in wall-clock), so the fade and
    // the clip's tail overlap (we don't fade out of a frozen last
    // frame). With playback_rate > 1 the clip plays faster, so we need
    // `blend_out_seconds × rate` of clip-time remaining to give us
    // `blend_out_seconds` of wall-clock for the fade. If the clip is
    // shorter than blend_in + blend_out we'll pass through Hold briefly
    // or skip it; the math degrades gracefully.
    const float blend_out_clip_time = s.one_shot_blend_out_seconds * s.one_shot_playback_rate;
    switch (s.one_shot_phase)
    {
    case OneShotPhase::Inactive:
        s.one_shot_weight = 0.0f;
        break;
    case OneShotPhase::BlendIn:
    {
        if (s.one_shot_blend_in_seconds <= 0.0f)
        {
            s.one_shot_weight = 1.0f;
            s.one_shot_phase = OneShotPhase::Hold;
        }
        else
        {
            s.one_shot_weight =
                std::min(1.0f, s.one_shot_weight + dt / s.one_shot_blend_in_seconds);
            if (s.one_shot_weight >= 1.0f)
                s.one_shot_phase = OneShotPhase::Hold;
        }
        // Auto-advance to BlendOut when reaching clip-end window — but
        // never if freeze_last is set; held actions wait for an
        // explicit releaseOneShot() call.
        const float dur = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
        if (!s.one_shot_freeze_last && dur > 0.0f &&
            s.one_shot.time_seconds >= dur - blend_out_clip_time)
            s.one_shot_phase = OneShotPhase::BlendOut;
        break;
    }
    case OneShotPhase::Hold:
    {
        const float dur = s.one_shot.animation ? s.one_shot.animation->duration() : 0.0f;
        if (!s.one_shot_freeze_last &&
            (one_shot_finished ||
             (dur > 0.0f && s.one_shot.time_seconds >= dur - blend_out_clip_time)))
            s.one_shot_phase = OneShotPhase::BlendOut;
        break;
    }
    case OneShotPhase::BlendOut:
    {
        if (s.one_shot_blend_out_seconds <= 0.0f)
        {
            s.one_shot_weight = 0.0f;
            s.one_shot_phase = OneShotPhase::Inactive;
            s.one_shot.animation = nullptr;
            // No discontinuity reset needed: extractHipDelta(one_shot)
            // sees animation==nullptr and resets the track's tracking
            // automatically next frame.
        }
        else
        {
            s.one_shot_weight =
                std::max(0.0f, s.one_shot_weight - dt / s.one_shot_blend_out_seconds);
            if (s.one_shot_weight <= 0.0f)
            {
                s.one_shot_phase = OneShotPhase::Inactive;
                s.one_shot.animation = nullptr;
            }
        }
        break;
    }
    }

    // ---- Sample all three tracks ----
    {
        ZoneScopedN("sample-tracks");
        s.loco_current.sample();
        s.loco_previous.sample();
        s.one_shot.sample();
    }

    // ---- Per-track hip XZ delta (root-motion source) ----
    //
    // Each track captures its own clip's pre-freeze hip XZ in
    // local-joint space and computes a per-frame delta. The exposed
    // PoseSampler::consumedHipDelta() blends these deltas weighted by
    // the current loco-blend and one-shot weights — that's the
    // velocity-level blend.
    //
    // Why not blend at the pose level (read hip from final_locals)?
    // Because cross-fading between two clips whose hips are at different
    // absolute positions creates a phantom motion: the blended hip
    // position interpolates from "at clipA's hip XZ" to "at clipB's
    // hip XZ," which the delta computation reads as motion that
    // neither clip authored. Velocity-level blend avoids this — each
    // track contributes its own (correct, clip-authored) delta and we
    // just sum them.
    //
    // Stationary loops (idle, blocking, etc. — anything below the
    // gait-cycle hip-path threshold) contribute zero delta regardless
    // of any micro-sway. This keeps the player anchored when standing
    // still.
    if (s.hips_joint_idx >= 0)
    {
        ZoneScopedN("hip-delta-extract");
        constexpr float kGaitCycleThreshold = 0.5f;
        const int hip_soa = s.hips_joint_idx / 4;
        const int hip_lane = s.hips_joint_idx % 4;

        auto extractHipDelta = [&](Track& t)
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
            // Stationary clips contribute zero gameplay delta. Avoids
            // idle-sway drift, blocking-pose drift, and whatever else
            // a Mixamo clip authors as "in-place" with a tiny residual.
            const bool stationary =
                t.hip_path_cached >= 0.0f && t.hip_path_cached < kGaitCycleThreshold;
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
                // Discontinuity guard: a >0.5m delta in one frame is
                // a clip-restart wrap or a phase reset. Emit zero and
                // re-baseline. Real walk/run motion is a few cm/frame
                // at 60Hz; the threshold catches resets.
                const bool discontinuity = std::abs(d.x) > 0.5f || std::abs(d.y) > 0.5f;
                if (discontinuity)
                    t.last_hip_delta = glm::vec3(0.0f);
                else
                    t.last_hip_delta = glm::vec3(d.x, 0.0f, d.y);
            }
            else
            {
                t.last_hip_delta = glm::vec3(0.0f);
            }
            t.last_hip_xz = cur;
            t.last_hip_xz_valid = true;
        };
        extractHipDelta(s.loco_current);
        extractHipDelta(s.loco_previous);
        extractHipDelta(s.one_shot);
    }

    // ---- Blend 1: locomotion pose = loco_current + loco_previous ----
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
        if (!loco_blend.Run())
            return false;
    }

    // ---- Blend 2: final pose = locomotion pose vs one-shot pose ----
    //
    // Fast path: when no one-shot is active, skip the second blend
    // entirely and copy loco_blended into final_locals. This avoids any
    // possibility of the second BlendingJob introducing artefacts on
    // joints that should be untouched (legs especially, when no attack
    // is in flight). Profile-wise it's also the common case — we spend
    // most frames in pure-locomotion state.
    if (s.one_shot.animation == nullptr)
    {
        ZoneScopedN("blend-final-fast");
        s.final_locals = s.loco_blended;
    }
    else
    {
        ZoneScopedN("blend-final");
        // Layer setup:
        //   * Locomotion: layer.weight = 1.0 always, no joint_weights (= 1
        //     per joint). Locomotion contributes a constant 1.0 everywhere,
        //     so the per-joint accumulated weight always exceeds ozz's
        //     rest-pose threshold — the legs never "fall back to T-pose"
        //     when the one-shot peaks.
        //   * One-shot: layer.weight = 1.0 always, joint_weights filled
        //     per-frame as (mask × one_shot_weight). The blend is driven
        //     entirely through the one-shot's *per-joint* weights.
        //
        // Net per-joint result:
        //   * Lower body (mask=0): loco contributes 1, one-shot 0. Pure loco.
        //   * Upper body (mask=1): loco contributes 1, one-shot contributes
        //     one_shot_weight. ozz normalizes → upper body crossfades from
        //     loco to one-shot as one_shot_weight ramps 0 → 1.
        //
        // Full-body one-shots (Heavy/Running, mask all 1.0) reduce to the
        // standard crossfade since every lane has mask=1.
        const std::size_t n_soa = s.one_shot_joint_weights.size();
        const ozz::math::SimdFloat4 w_simd = ozz::math::simd_float4::Load1(s.one_shot_weight);
        if (s.one_shot_mask == PoseSampler::BodyMask::UpperBody && !s.upper_body_weights.empty())
        {
            for (std::size_t i = 0; i < n_soa; ++i)
                s.one_shot_joint_weights[i] = s.upper_body_weights[i] * w_simd;
        }
        else
        {
            // Full-body: every lane = one_shot_weight.
            for (std::size_t i = 0; i < n_soa; ++i)
                s.one_shot_joint_weights[i] = w_simd;
        }

        // Layer-0 (locomotion) weight depends on the body mask:
        //
        //   * UpperBody mask: keep locomotion at 1.0. The one-shot's
        //     per-joint mask zeroes its leg contribution, so the legs
        //     stay pure loco (loco=1, one-shot=0 → normalized to loco).
        //     Upper-body joints have one-shot mask=1 and loco=1, so they
        //     crossfade as one_shot_weight ramps 0→1, peaking at 50/50
        //     at full ramp — that's the intended attack-over-walk look:
        //     arms swing, gait keeps cycling.
        //
        //   * Full mask (dodge, heavy attacks, running attacks): the
        //     one-shot SHOULD fully take over. Locomotion ramps 1 → 0 as
        //     one-shot ramps 0 → 1, mirroring the one-shot weight. Net
        //     per-joint result: at peak the output is pure one-shot, no
        //     residual loco bleed pulling the prone-hip back to standing.
        const float loco_weight = (s.one_shot_mask == PoseSampler::BodyMask::Full)
                                      ? (1.0f - s.one_shot_weight)
                                      : 1.0f;
        ozz::animation::BlendingJob::Layer final_layers[2];
        final_layers[0].weight = loco_weight;
        final_layers[0].transform = ozz::make_span(s.loco_blended);
        final_layers[1].weight = 1.0f;
        final_layers[1].transform = ozz::make_span(s.one_shot.local_transforms);
        final_layers[1].joint_weights = ozz::make_span(s.one_shot_joint_weights);

        ozz::animation::BlendingJob final_blend;
        final_blend.layers = ozz::span<const ozz::animation::BlendingJob::Layer>(final_layers, 2);
        final_blend.rest_pose = s.skeleton->joint_rest_poses();
        final_blend.output = ozz::make_span(s.final_locals);
        if (!final_blend.Run())
            return false;
    }

    // ---- Inertialization step 2 — compute offset / apply decay ----
    //
    // If we captured this frame, finalize the offset: pre_change_locals
    // currently holds the previous-frame pose; subtract the current
    // post-blend pose to get the per-joint offset. Store back into
    // pre_change_locals (now repurposed as the static offset). Start
    // the decay timer.
    //
    // For frames AFTER the capture frame (decay still active), apply
    // the stored offset scaled by a cubic ease-out weight. Joints
    // visibly continue from where they were and curve smoothly into
    // the new clip's authored motion.
    //
    // Math per SoA-joint:
    //   * Translation: linear: out_t = clip_t + offset_t * w
    //   * Rotation:    quat-mul: out_q = NLerp(identity, offset_q, w) * clip_q
    //                  (NLerp is fine for small angle offsets and avoids
    //                  the cost / branching of true Slerp.)
    //   * Scale:       linear: out_s = clip_s * (1 + (offset_s_ratio - 1) * w)
    //                  but in practice scale is rarely animated; we just
    //                  leave it untouched.
    if (capture_this_frame)
    {
        const std::size_t n_soa = s.final_locals.size();
        // Per-joint duration scaling: each joint's window = base +
        // scale * |offset_radians|, clamped to max. Offset magnitude
        // approximated as 2*|q.xyz| (≈ θ for small angles, conservative
        // for large). Translation offset isn't included in the metric;
        // joint translations are usually tiny in animation, and the
        // visible "leg snap" we're targeting is rotation-driven.
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

            // Per-lane offset magnitude (radians ≈ 2*|q.xyz|).
            const ozz::math::SimdFloat4 xyz_sq = off.rotation.x * off.rotation.x +
                                                 off.rotation.y * off.rotation.y +
                                                 off.rotation.z * off.rotation.z;
            // sqrt via reciprocal-sqrt-est: |xyz| = xyz_sq * rsqrt(xyz_sq).
            // Guard against zero with a small epsilon to avoid Inf.
            const ozz::math::SimdFloat4 eps =
                ozz::math::simd_float4::Load1(1e-8f);
            const ozz::math::SimdFloat4 safe = xyz_sq + eps;
            const ozz::math::SimdFloat4 inv_len_xyz = ozz::math::RSqrtEst(safe);
            const ozz::math::SimdFloat4 xyz_len = safe * inv_len_xyz; // |xyz|
            const ozz::math::SimdFloat4 angle = two_simd * xyz_len;
            ozz::math::SimdFloat4 dur = base_simd + scale_simd * angle;
            // Clamp to max.
            dur = ozz::math::Min(dur, max_simd);
            s.decay_duration_per_joint[i] = dur;
            s.decay_elapsed_per_joint[i] = ozz::math::simd_float4::zero();
        }
        s.decay_active = true;
        s.decay_elapsed = 0.0f;
    }
    if (s.decay_active && s.decay_max_seconds > 0.0f)
    {
        s.decay_elapsed += dt;
        // Decay deactivates once the longest possible window has
        // passed. Per-joint short windows clamp their weight to 0
        // earlier, so they stop contributing on schedule.
        if (s.decay_elapsed >= s.decay_max_seconds)
        {
            s.decay_active = false;
        }
        else
        {
            const ozz::math::SimdFloat4 dt_simd = ozz::math::simd_float4::Load1(dt);
            const ozz::math::SimdFloat4 zero_simd = ozz::math::simd_float4::zero();
            const ozz::math::SimdFloat4 one_simd = ozz::math::simd_float4::Load1(1.0f);
            const std::size_t n_soa = s.final_locals.size();
            for (std::size_t i = 0; i < n_soa; ++i)
            {
                s.decay_elapsed_per_joint[i] = s.decay_elapsed_per_joint[i] + dt_simd;
                const ozz::math::SimdFloat4 dur = s.decay_duration_per_joint[i];
                const ozz::math::SimdFloat4 elapsed = s.decay_elapsed_per_joint[i];
                // Avoid div-by-zero on lanes where duration is 0.
                const ozz::math::SimdFloat4 dur_safe =
                    ozz::math::Max(dur, ozz::math::simd_float4::Load1(1e-6f));
                // u = clamp(1 - elapsed/duration, 0, 1)
                ozz::math::SimdFloat4 u = one_simd - elapsed * ozz::math::RcpEst(dur_safe);
                u = ozz::math::Max(u, zero_simd);
                u = ozz::math::Min(u, one_simd);
                // Lanes where duration was 0 get u=0 too — no decay
                // applied (they were already at-target at capture).
                ozz::math::SimdFloat4 w_simd = u * u * u;
                // Mask off lanes whose original duration was zero.
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
    }

    // ---- Visual hip XZ freeze (X/Z only, preserve Y bob) ----
    // Anchor the visible bones to the model origin so the character's
    // body doesn't drift relative to its world transform. The
    // gameplay-side delta is computed per-track above (velocity-level
    // blend); this freeze is purely visual.
    if (s.hips_joint_idx >= 0)
    {
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

    // ---- Local-to-model ----
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
        if (!ljob.Run())
            return false;
    }

    // ---- GPU palette: skin_matrix[i] = current_model[i] * inverse_bind[i] ----
    {
        ZoneScopedN("gpu-palette");
        const std::size_t bone_count = s.model_matrices.size();
        for (std::size_t i = 0; i < bone_count; ++i)
        {
            glm::mat4 current_model;
            std::memcpy(&current_model, &s.model_matrices[i], sizeof(glm::mat4));
            const glm::mat4 inv_bind = (i < s.inverse_bind_matrices.size())
                                           ? s.inverse_bind_matrices[i]
                                           : glm::mat4(1.0f);
            bone_palette[i] = current_model * inv_bind;
        }
    }
    // Save the post-blend pose for next frame's potential
    // inertialization capture. This is what was actually rendered
    // (after blend, after hip-freeze, after any decay applied this
    // frame).
    s.post_locals_last = s.final_locals;
    return true;
}

} // namespace selva::anim
