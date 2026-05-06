#pragma once

#include <memory>
#include <string>

namespace ozz::animation
{
class Animation;
}

namespace selva::anim
{

// A loaded animation clip — keyframed bone tracks over a fixed duration.
// Can be sampled at any time t ∈ [0, duration]. Owns the underlying ozz
// data via unique_ptr (heavy and non-copyable like Skeleton).
//
// One AnimationClip per (skeleton, motion) pair. Multiple clips can be
// loaded for the same skeleton (idle, walk, dodge, attack, ...).
struct AnimationClip
{
    std::unique_ptr<ozz::animation::Animation> ozz_animation;

    AnimationClip();
    AnimationClip(const AnimationClip&) = delete;
    AnimationClip& operator=(const AnimationClip&) = delete;
    AnimationClip(AnimationClip&&) noexcept;
    AnimationClip& operator=(AnimationClip&&) noexcept;
    ~AnimationClip();

    // Total clip length in seconds. 0 if not loaded.
    float duration() const;

    // Number of animated tracks (one per joint that the clip affects).
    // Zero if not loaded.
    int trackCount() const;

    bool isLoaded() const;
};

// Load a clip from a pre-converted .ozz binary. Returns an empty
// (isLoaded() == false) clip on failure and logs to stderr; callers should
// check before using.
AnimationClip loadAnimationClip(const std::string& path);

} // namespace selva::anim
