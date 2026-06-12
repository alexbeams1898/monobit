#include "anim/ClipGeometry.h"

#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_float4x4.h>
#include <ozz/base/maths/soa_transform.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace selva::anim
{

namespace
{

int findJointByName(const ozz::animation::Skeleton& skel, const char* name)
{
    if (name == nullptr || *name == '\0')
        return -1;
    const auto names = skel.joint_names();
    const int n = skel.num_joints();
    for (int i = 0; i < n; ++i)
    {
        if (names[i] != nullptr && std::strcmp(names[i], name) == 0)
            return i;
    }
    return -1;
}

// Sampling context the caller pre-allocates and reuses across many
// time-samples of the same clip+skeleton (avoids per-call alloc).
struct PairSampleCtx
{
    const ozz::animation::Animation* anim;
    const ozz::animation::Skeleton* skel;
    ozz::animation::SamplingJob::Context* ctx;
    std::vector<ozz::math::SoaTransform>* locals;
    std::vector<ozz::math::Float4x4>* models;
    const ozz::math::Float4x4* root_storage;
    int joint_a;
    int joint_b;
    float dur;
};

struct PairSampleResult
{
    float ax;
    float az;
    float bx;
    float bz;
};

// Sample `anim` at `t` (clamped to [0, dur]) and read model-space
// positions of joint_a/joint_b into out. False on ozz job failure or
// invalid input (dur <= 0).
bool samplePairAtTime(const PairSampleCtx& c, float t, PairSampleResult& out)
{
    if (c.dur <= 0.0f)
        return false;
    const float ratio = std::clamp(t / c.dur, 0.0f, 1.0f);
    ozz::animation::SamplingJob sjob;
    sjob.animation = c.anim;
    sjob.context = c.ctx;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(*c.locals);
    if (!sjob.Run())
        return false;
    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = c.skel;
    ljob.root = c.root_storage;
    ljob.input = ozz::make_span(*c.locals);
    ljob.output = ozz::make_span(*c.models);
    if (!ljob.Run())
        return false;
    alignas(16) float a_col3[4];
    alignas(16) float b_col3[4];
    ozz::math::StorePtr((*c.models)[c.joint_a].cols[3], a_col3);
    ozz::math::StorePtr((*c.models)[c.joint_b].cols[3], b_col3);
    out.ax = a_col3[0];
    out.az = a_col3[2];
    out.bx = b_col3[0];
    out.bz = b_col3[2];
    return true;
}

} // namespace

JointReachResult computeJointReach(const AnimationClip& clip, const Skeleton& skeleton,
                                   const char* hips_joint_name, const char* target_joint_name,
                                   float window_start_seconds, float window_end_seconds,
                                   float sample_hz)
{
    JointReachResult result;
    if (!clip.isLoaded())
        return result;
    const ozz::animation::Animation* anim = clip.ozz_animation.get();
    if (anim == nullptr)
        return result;
    if (skeleton.ozz_skeleton == nullptr)
        return result;
    const ozz::animation::Skeleton& skel = *skeleton.ozz_skeleton;
    const int hip_idx = findJointByName(skel, hips_joint_name);
    const int target_idx = findJointByName(skel, target_joint_name);
    if (hip_idx < 0 || target_idx < 0)
        return result;
    const float dur = anim->duration();
    if (dur <= 0.0f)
        return result;
    const float t_start = std::max(0.0f, window_start_seconds);
    const float t_end = std::min(dur, window_end_seconds);
    if (t_end <= t_start)
        return result;
    if (sample_hz <= 0.0f)
        return result;

    // Identity root: we want raw model-space joint positions, no
    // pre-rotation. The XZ delta between hip and target is the
    // rig-relative reach measurement.
    const ozz::math::Float4x4 root_storage = ozz::math::Float4x4::identity();

    ozz::animation::SamplingJob::Context ctx;
    ctx.Resize(skel.num_joints());
    std::vector<ozz::math::SoaTransform> locals(skel.num_soa_joints());
    std::vector<ozz::math::Float4x4> models(skel.num_joints());

    const PairSampleCtx pair{anim,          &skel,   &ctx,       &locals, &models,
                             &root_storage, hip_idx, target_idx, dur};
    const float step = 1.0f / sample_hz;
    const int n_steps = static_cast<int>(std::ceil((t_end - t_start) / step)) + 1;
    float xz_max_sq = 0.0f;
    float xz_at_end = 0.0f;
    int taken = 0;
    for (int i = 0; i < n_steps; ++i)
    {
        const float t = std::min(t_start + static_cast<float>(i) * step, t_end);
        PairSampleResult s;
        if (!samplePairAtTime(pair, t, s))
            continue;
        const float dx = s.bx - s.ax;
        const float dz = s.bz - s.az;
        const float xz_sq = dx * dx + dz * dz;
        if (xz_sq > xz_max_sq)
            xz_max_sq = xz_sq;
        xz_at_end = std::sqrt(xz_sq);
        ++taken;
    }
    result.xz_max_meters = std::sqrt(xz_max_sq);
    result.xz_at_window_end_meters = xz_at_end;
    result.sample_count = taken;
    return result;
}

} // namespace selva::anim
