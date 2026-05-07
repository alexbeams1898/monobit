#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace selva::anim
{

struct AnimationClip;
struct Skeleton;
struct SkeletalMesh;

// PoseSampler — produces the GPU bone palette for an animated character,
// given a sequence of "play this clip" requests over time. Owns scratch
// buffers and per-clip ozz contexts so per-frame sampling is allocation-
// free in steady state.
//
// One PoseSampler per animated character. The sampler manages two
// concurrent clips internally — the *current* one (playing fully at the
// end of any blend) and the *previous* one (fading out). When the caller
// switches the active clip, a cross-fade ramps from previous→current over
// `blend_seconds`. Both clips advance during the blend so motion stays
// continuous. Setting `blend_seconds = 0` snaps with no fade.
//
// Lifecycle: construct via createPoseSampler(skeleton, mesh). Then call
// update(clip, dt, blend_seconds) every frame to advance the sampler;
// read bone_palette from the renderer.
struct PoseSampler
{
    struct Impl; // ozz state lives here so the header doesn't pull ozz
    std::unique_ptr<Impl> impl;

    // The bone palette in **ozz joint order**, model-space. Updated by
    // update(); read by the renderer. Length = skeleton bone count.
    std::vector<glm::mat4> bone_palette;

    PoseSampler();
    PoseSampler(const PoseSampler&) = delete;
    PoseSampler& operator=(const PoseSampler&) = delete;
    PoseSampler(PoseSampler&&) noexcept;
    PoseSampler& operator=(PoseSampler&&) noexcept;
    ~PoseSampler();

    // Advance the sampler by `dt` seconds and update bone_palette.
    //   * `clip` is the desired active clip this frame. If it differs
    //     from the currently-active clip, a cross-fade kicks off:
    //     previous keeps playing, new starts at t=0, weight ramps over
    //     `blend_seconds` until the new fully owns the output.
    //   * `dt` advances all active clips' internal times (looping at
    //     each clip's duration).
    //   * `blend_seconds` is the duration of any *new* blend that starts
    //     this frame. An in-progress blend keeps its original duration.
    //     Pass 0 to snap (no fade) — useful at startup or for hard cuts.
    // Returns false if the inputs are invalid (no skeleton, no clip).
    bool update(const AnimationClip& clip, float dt, float blend_seconds);
};

// Build a PoseSampler bound to the given skeleton + mesh. The skeleton
// and mesh must outlive the returned sampler.
PoseSampler createPoseSampler(const Skeleton& skeleton, const SkeletalMesh& mesh);

} // namespace selva::anim
