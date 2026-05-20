#pragma once

#include <cstdint>
#include <memory>
#include <string>

// Forward-declare ozz types so users of this header don't pull in ozz's
// (sizeable) public headers — the .cpp implementation will include them.
namespace ozz::animation
{
class Skeleton;
}

namespace selva::anim
{

// A loaded skeleton — a hierarchy of named bones in their bind pose. Owned
// by a single std::unique_ptr because ozz's Skeleton type isn't copyable
// and is heavy enough that we don't want it on the stack.
//
// One Skeleton per character mesh family (e.g. "humanoid"). Multiple
// AnimationClips can play on the same Skeleton.
struct Skeleton
{
    std::unique_ptr<ozz::animation::Skeleton> ozz_skeleton;

    Skeleton();
    Skeleton(const Skeleton&) = delete;
    Skeleton& operator=(const Skeleton&) = delete;
    Skeleton(Skeleton&&) noexcept;
    Skeleton& operator=(Skeleton&&) noexcept;
    ~Skeleton();

    // Number of bones (including the root). Zero if not loaded.
    int boneCount() const;

    // Was a skeleton successfully loaded? Useful for graceful fallback
    // (game stays runnable if a checkout is missing assets).
    bool isLoaded() const;
};

// Load a skeleton from a pre-converted .ozz binary (produced at build time
// by the gltf2ozz tool). Path is relative to the working directory.
// Returns an empty (isLoaded() == false) skeleton on failure and logs to
// stderr; callers should check before using.
Skeleton loadSkeleton(const std::string& path);

} // namespace selva::anim
