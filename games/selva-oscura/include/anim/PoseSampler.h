#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace selva::anim
{

struct AnimationClip;
struct Skeleton;
struct SkeletalMesh;

// PoseSampler — given a skeleton and a clip, produces the bone palette
// (model-space bone matrices) for any time t. Owns per-instance scratch
// buffers so sampling is allocation-free per frame.
//
// One PoseSampler per animated character. If multiple characters play
// different clips, each has its own sampler — the sampler caches the
// last-sampled clip's interpolation state.
//
// Lifecycle: construct via createPoseSampler(skeleton). The skeleton must
// outlive the sampler. Then call sample(clip, time_seconds) any number
// of times to update the bone_palette.
struct PoseSampler
{
    struct Impl; // ozz state lives here so the header doesn't pull ozz
    std::unique_ptr<Impl> impl;

    // The bone palette in **ozz joint order**, model-space. Updated by
    // sample(); read by the renderer. Length = skeleton bone count.
    // Matrices are stored as glm::mat4 (column-major, identical layout
    // to ozz::math::Float4x4 underneath).
    std::vector<glm::mat4> bone_palette;

    PoseSampler();
    PoseSampler(const PoseSampler&) = delete;
    PoseSampler& operator=(const PoseSampler&) = delete;
    PoseSampler(PoseSampler&&) noexcept;
    PoseSampler& operator=(PoseSampler&&) noexcept;
    ~PoseSampler();

    // Sample `clip` at `time_seconds` (clamped to [0, clip.duration()])
    // and update bone_palette. Returns false if the inputs are invalid
    // (mismatched skeleton, unloaded clip, etc.).
    bool sample(const AnimationClip& clip, float time_seconds);
};

// Build a PoseSampler bound to the given skeleton + mesh. The skeleton
// and mesh must outlive the returned sampler. Pairing them at construction
// is deliberate — the sampler's bone palette must be computed in the same
// space as the mesh's baked vertex positions; taking both here makes that
// wiring impossible to forget. Pass an unloaded mesh (or a different
// overload — TBD) only for skeletons rendered without a glTF mesh, which
// we don't have today.
PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh);

} // namespace selva::anim
