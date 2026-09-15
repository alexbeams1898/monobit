#include "anim/AnimationClip.h"
#include "anim/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

using selva::anim::AnimationClip;
using selva::anim::loadAnimationClip;
using selva::anim::loadSkeleton;
using selva::anim::Skeleton;

// These tests run from the test exe's working directory, which CMake sets
// to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} (build/bin) by default for executable
// targets. The .ozz files live at build/bin/assets/characters/x_bot/...
// because the gltf2ozz custom command writes them there at build time.
//
// If a test fails with "cannot open" or "not a skeleton archive", the most
// likely cause is the asset path: tests run from CWD wherever ctest invoked
// them. Run via `ctest --test-dir build` — ctest sets CWD to the test's
// build dir. If invoked manually, run from build/bin.

namespace
{
const char* kSkeletonPath = "assets/characters/x_bot/skeleton.ozz";
const char* kIdlePath = "assets/characters/x_bot/sword_and_shield_idle.ozz";
const char* kWalkPath = "assets/characters/x_bot/sword_and_shield_walk.ozz";
} // namespace

TEST_CASE("Skeleton loads from X Bot skeleton.ozz", "[anim][skeleton][load]")
{
    const Skeleton skel = loadSkeleton(kSkeletonPath);
    REQUIRE(skel.isLoaded());
    // X Bot ships with a 65-joint humanoid rig. Test the lower
    // bound (in case of small rig variants) rather than strict equality.
    REQUIRE(skel.boneCount() >= 49);
    // Sanity ceiling — we'd notice if something exploded the count.
    REQUIRE(skel.boneCount() < 200);
}

TEST_CASE("AnimationClip loads from sword_and_shield_idle.ozz", "[anim][clip][load]")
{
    const AnimationClip clip = loadAnimationClip(kIdlePath);
    REQUIRE(clip.isLoaded());
    REQUIRE(clip.trackCount() > 0);
    // Real animations are at least a fraction of a second long. 0 would
    // suggest the clip loaded structurally but has no keyframes.
    REQUIRE(clip.duration() > 0.0f);
    REQUIRE(clip.duration() < 60.0f); // sanity ceiling
}

TEST_CASE("AnimationClip loads from sword_and_shield_walk.ozz", "[anim][clip][load]")
{
    const AnimationClip clip = loadAnimationClip(kWalkPath);
    REQUIRE(clip.isLoaded());
    REQUIRE(clip.trackCount() > 0);
    REQUIRE(clip.duration() > 0.0f);
}

TEST_CASE("Idle and Walk clips share the same skeleton track count", "[anim][clip][load]")
{
    // Both clips were exported against the same X Bot skeleton (via the
    // retarget step), so they should have the same number of animated
    // tracks. This is a foundational assumption for blending.
    const AnimationClip idle = loadAnimationClip(kIdlePath);
    const AnimationClip walk = loadAnimationClip(kWalkPath);
    REQUIRE(idle.isLoaded());
    REQUIRE(walk.isLoaded());
    REQUIRE(idle.trackCount() == walk.trackCount());
}

TEST_CASE("loadSkeleton returns empty result on missing path", "[anim][skeleton][robustness]")
{
    const Skeleton skel = loadSkeleton("nonexistent/path.ozz");
    REQUIRE_FALSE(skel.isLoaded());
    REQUIRE(skel.boneCount() == 0);
}

TEST_CASE("loadAnimationClip returns empty result on missing path", "[anim][clip][robustness]")
{
    const AnimationClip clip = loadAnimationClip("nonexistent/path.ozz");
    REQUIRE_FALSE(clip.isLoaded());
    REQUIRE(clip.trackCount() == 0);
}
