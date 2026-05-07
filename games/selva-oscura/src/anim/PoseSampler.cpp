#include "anim/PoseSampler.h"

#include "anim/AnimationClip.h"
#include "anim/SkeletalMesh.h"
#include "anim/Skeleton.h"

#include <cmath>
#include <cstring>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/blending_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

namespace selva::anim
{

// ---------------------------------------------------------------------------
// Two-track sampler with cross-fade. Each track owns:
//   * a pointer to the currently-assigned ozz Animation,
//   * an ozz SamplingJob::Context (caches keyframe interpolation state),
//   * a local-space SoaTransform output buffer,
//   * its own time clock (advanced by dt every frame, wraps at duration).
//
// The "active" track is the clip the gameplay code last asked for. The
// "previous" track is the one we're fading away from. When a fade is in
// progress, both tracks advance and a BlendingJob mixes their local
// transforms by weight before LocalToModelJob produces the bone palette.
//
// When the active track is nullptr (game hasn't asked for a clip yet),
// or when blending is finished and previous is dropped, only one track
// participates. Code paths are simpler when symmetric so we always run
// the BlendingJob — its weights handle the degenerate cases.
// ---------------------------------------------------------------------------

namespace
{
struct Track
{
    const ozz::animation::Animation* animation = nullptr;
    ozz::animation::SamplingJob::Context context;
    std::vector<ozz::math::SoaTransform> local_transforms;
    float time_seconds = 0.0f;

    void resize(int num_joints, int num_soa_joints)
    {
        context.Resize(num_joints);
        local_transforms.resize(num_soa_joints);
    }

    // Sample at the current time. Returns false if the clip has none.
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
} // namespace

struct PoseSampler::Impl
{
    const ozz::animation::Skeleton* skeleton = nullptr;

    // Two tracks — current (the one gameplay asked for) and previous
    // (the one fading out). Either or both may have a null animation.
    Track current;
    Track previous;

    // Blend state. weight = how much of `current` is blended in.
    // 0 = all previous, 1 = all current. duration = how long the in-
    // progress fade should take; elapsed = how far through it we are.
    // When elapsed >= duration, blending is complete: we copy current
    // → previous semantics by clearing previous (no fading-from), and
    // weight is effectively 1 forever after.
    float blend_weight = 1.0f;
    float blend_elapsed = 0.0f;
    float blend_duration = 0.0f;

    glm::mat4 root_transform = glm::mat4(1.0f);
    std::vector<glm::mat4> inverse_bind_matrices;

    // Scratch buffers for the blend output and the model-space matrices.
    std::vector<ozz::math::SoaTransform> blended_locals;
    std::vector<ozz::math::Float4x4> model_matrices;
};

PoseSampler::PoseSampler() : impl(std::make_unique<Impl>())
{
}
PoseSampler::PoseSampler(PoseSampler&&) noexcept = default;
PoseSampler& PoseSampler::operator=(PoseSampler&&) noexcept = default;
PoseSampler::~PoseSampler() = default;

PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh)
{
    PoseSampler sampler;
    if (!skeleton.isLoaded())
        return sampler;

    const ozz::animation::Skeleton& ozz_skel = *skeleton.ozz_skeleton;
    const int n_joints = ozz_skel.num_joints();
    const int n_soa = ozz_skel.num_soa_joints();

    sampler.impl->skeleton = &ozz_skel;
    sampler.impl->current.resize(n_joints, n_soa);
    sampler.impl->previous.resize(n_joints, n_soa);
    sampler.impl->blended_locals.resize(n_soa);
    sampler.impl->model_matrices.resize(n_joints);
    sampler.bone_palette.resize(n_joints);

    sampler.impl->root_transform = mesh.asset_root_transform;
    sampler.impl->inverse_bind_matrices = mesh.inverse_bind_matrices;
    return sampler;
}

bool PoseSampler::update(const AnimationClip& clip, float dt, float blend_seconds)
{
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return false;

    Impl& s = *impl;
    const ozz::animation::Animation* desired = clip.ozz_animation.get();

    // Active-clip change: shift current → previous and start a new blend.
    // Snap immediately if no blend was requested.
    if (desired != s.current.animation)
    {
        if (blend_seconds > 0.0f && s.current.animation != nullptr)
        {
            // Swap tracks rather than move: ozz SamplingJob::Context is
            // non-movable (heap cache, internal pointers) so we keep both
            // contexts in place and just swap which one each track uses
            // by swapping the lighter members (animation, time, transforms).
            // Both contexts were already sized at createPoseSampler().
            std::swap(s.current.animation, s.previous.animation);
            std::swap(s.current.time_seconds, s.previous.time_seconds);
            s.current.local_transforms.swap(s.previous.local_transforms);
            s.blend_weight = 0.0f;
            s.blend_elapsed = 0.0f;
            s.blend_duration = blend_seconds;
        }
        else
        {
            // Snap: drop any previous, fully active.
            s.previous.animation = nullptr;
            s.blend_weight = 1.0f;
            s.blend_elapsed = 0.0f;
            s.blend_duration = 0.0f;
        }
        s.current.animation = desired;
        s.current.time_seconds = 0.0f;
    }

    // Advance times for both tracks. Wrap inside each clip's duration.
    auto advance_time = [&](Track& t)
    {
        if (!t.animation)
            return;
        t.time_seconds += dt;
        const float dur = t.animation->duration();
        if (dur > 0.0f && t.time_seconds >= dur)
            t.time_seconds = std::fmod(t.time_seconds, dur);
    };
    advance_time(s.current);
    advance_time(s.previous);

    // Advance blend.
    if (s.blend_duration > 0.0f)
    {
        s.blend_elapsed += dt;
        if (s.blend_elapsed >= s.blend_duration)
        {
            s.blend_weight = 1.0f;
            s.blend_elapsed = 0.0f;
            s.blend_duration = 0.0f;
            s.previous.animation = nullptr;
        }
        else
        {
            s.blend_weight = s.blend_elapsed / s.blend_duration;
        }
    }

    // Sample both tracks (cheap if their animation is null — the lambda
    // skips and the BlendingJob layer just gets weight 0).
    s.current.sample();
    s.previous.sample();

    // Blend. ozz's BlendingJob requires `rest_pose` so it has something
    // to fall back to when the layer weights don't sum to enough; we use
    // the skeleton's joint rest poses (provided by ozz directly).
    ozz::animation::BlendingJob::Layer layers[2];
    layers[0].weight = s.blend_weight;
    layers[0].transform = ozz::make_span(s.current.local_transforms);
    layers[1].weight = (1.0f - s.blend_weight) * (s.previous.animation ? 1.0f : 0.0f);
    layers[1].transform = ozz::make_span(s.previous.local_transforms);

    ozz::animation::BlendingJob bjob;
    bjob.layers = ozz::span<const ozz::animation::BlendingJob::Layer>(layers, 2);
    bjob.rest_pose = s.skeleton->joint_rest_poses();
    bjob.output = ozz::make_span(s.blended_locals);
    if (!bjob.Run())
        return false;

    // Local-to-model.
    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &s.root_transform, sizeof(glm::mat4));

    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = s.skeleton;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(s.blended_locals);
    ljob.output = ozz::make_span(s.model_matrices);
    if (!ljob.Run())
        return false;

    // Build GPU palette: skin_matrix[i] = current_model[i] * inverse_bind[i].
    const std::size_t bone_count = s.model_matrices.size();
    for (std::size_t i = 0; i < bone_count; ++i)
    {
        glm::mat4 current_model;
        std::memcpy(&current_model, &s.model_matrices[i], sizeof(glm::mat4));
        const glm::mat4 inv_bind =
            (i < s.inverse_bind_matrices.size()) ? s.inverse_bind_matrices[i] : glm::mat4(1.0f);
        bone_palette[i] = current_model * inv_bind;
    }
    return true;
}

} // namespace selva::anim
