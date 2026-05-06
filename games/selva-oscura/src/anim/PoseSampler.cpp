#include "anim/PoseSampler.h"

#include "anim/AnimationClip.h"
#include "anim/SkeletalMesh.h"
#include "anim/Skeleton.h"

#include <cstring>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/span.h>

namespace selva::anim
{

// ---------------------------------------------------------------------------
// Impl — ozz scratch state. Hidden from the header so users don't pull in
// ozz's headers transitively.
//
// SamplingJob writes "SoA local-space transforms" — Structure-of-Arrays
// packed for SIMD. One SoaTransform holds 4 bones' worth of data; the
// number we need is ceil(num_bones / 4). Then LocalToModelJob converts
// that to model-space Float4x4 (one per bone, AoS).
//
// Both buffers are sized once based on the skeleton; reused every frame.
// ---------------------------------------------------------------------------
struct PoseSampler::Impl
{
    const ozz::animation::Skeleton* skeleton = nullptr;
    ozz::animation::SamplingJob::Context sampling_context;
    std::vector<ozz::math::SoaTransform> local_transforms; // size = num_soa_joints
    std::vector<ozz::math::Float4x4> model_matrices;       // size = num_joints
    // Asset-root transform sent to LocalToModelJob.root each frame.
    // Compensates for glTF scene-graph transforms above the skeleton's
    // root joint that gltf2ozz doesn't bake in. Stored as a glm::mat4
    // for storage compatibility with ozz::math::Float4x4 (same layout).
    glm::mat4 root_transform = glm::mat4(1.0f);
    // Inverse bind matrices, in ozz joint order. Multiplied per-bone
    // into the model-space matrices to produce the bind-relative
    // skin matrices the GPU shader actually wants.
    std::vector<glm::mat4> inverse_bind_matrices;
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
    sampler.impl->skeleton = &ozz_skel;
    // Resize the sampling context to handle the largest animation we'd
    // play on this skeleton — usually just the bone count itself.
    sampler.impl->sampling_context.Resize(ozz_skel.num_joints());
    sampler.impl->local_transforms.resize(ozz_skel.num_soa_joints());
    sampler.impl->model_matrices.resize(ozz_skel.num_joints());
    sampler.bone_palette.resize(ozz_skel.num_joints());
    // Bind the sampler's bone-palette space to the mesh's vertex space
    // by copying the asset root transform. From here, sampler and mesh
    // are guaranteed consistent — no manual wiring step.
    sampler.impl->root_transform = mesh.asset_root_transform;
    sampler.impl->inverse_bind_matrices = mesh.inverse_bind_matrices;
    return sampler;
}

bool PoseSampler::sample(const AnimationClip& clip, float time_seconds)
{
    if (!impl || !impl->skeleton || !clip.isLoaded())
        return false;

    const ozz::animation::Animation& anim = *clip.ozz_animation;

    // Step 1: SamplingJob — interpolate keyframes at this time. ozz wants
    // a *ratio* in [0, 1]; convert from seconds.
    const float duration = anim.duration();
    const float ratio = duration > 0.0f ? (time_seconds / duration) : 0.0f;

    ozz::animation::SamplingJob sjob;
    sjob.animation = &anim;
    sjob.context = &impl->sampling_context;
    sjob.ratio = ratio;
    sjob.output = ozz::make_span(impl->local_transforms);
    if (!sjob.Run())
        return false;

    // Step 2: LocalToModelJob — walk the skeleton, accumulating each bone's
    // model-space transform from its local + parent's model. Output is an
    // array of Float4x4 (column-major), one per bone, in ozz order.
    //
    // We pre-multiply by `root_transform` here. This is gltf2ozz's missing
    // step: gltf2ozz extracts the skeleton starting from the topmost
    // skinned joint (e.g. mixamorig:Hips), discarding any ancestor scene-
    // graph transforms. ozz's LocalToModelJob has a `root` parameter
    // exactly for this — it multiplies every output bone by the supplied
    // matrix, putting the whole palette into world meters (or whatever
    // the asset's intended space was) without any per-asset constant.
    ozz::math::Float4x4 root_storage;
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    std::memcpy(&root_storage, &impl->root_transform, sizeof(glm::mat4));

    ozz::animation::LocalToModelJob ljob;
    ljob.skeleton = impl->skeleton;
    ljob.root = &root_storage;
    ljob.input = ozz::make_span(impl->local_transforms);
    ljob.output = ozz::make_span(impl->model_matrices);
    if (!ljob.Run())
        return false;

    // Step 3: build the GPU bone palette. Each entry is the bone's
    // current model-space transform multiplied by its inverse bind
    // matrix:
    //
    //     skin_matrix[i] = current_model[i] * inverse_bind[i]
    //
    // This produces a "delta from rest pose" — applying it to a vertex
    // in its bind-pose position yields the vertex's current animated
    // position. ozz::math::Float4x4 has the same memory layout as
    // glm::mat4, so we can memcpy current_model into a glm::mat4 and
    // then multiply.
    static_assert(sizeof(ozz::math::Float4x4) == sizeof(glm::mat4),
                  "ozz::math::Float4x4 and glm::mat4 storage size differ");
    const std::size_t bone_count = impl->model_matrices.size();
    for (std::size_t i = 0; i < bone_count; ++i)
    {
        glm::mat4 current_model;
        std::memcpy(&current_model, &impl->model_matrices[i], sizeof(glm::mat4));
        const glm::mat4 inv_bind = (i < impl->inverse_bind_matrices.size())
                                       ? impl->inverse_bind_matrices[i]
                                       : glm::mat4(1.0f);
        bone_palette[i] = current_model * inv_bind;
    }
    return true;
}

} // namespace selva::anim
