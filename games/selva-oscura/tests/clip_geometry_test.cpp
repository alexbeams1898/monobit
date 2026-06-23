#include "anim/AnimationClip.h"
#include "anim/ClipGeometry.h"
#include "anim/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

using selva::anim::AnimationClip;
using selva::anim::computeJointReach;
using selva::anim::JointReachResult;
using selva::anim::loadAnimationClip;
using selva::anim::loadSkeleton;
using selva::anim::Skeleton;

// Tests run from build/bin (per skeletal_loader_test.cpp comment block).
// Humanoid skeleton + a known clip with a hand swing give us real data to
// scan. We reuse sword_and_shield_idle which is already used by the
// loader test, plus a swing clip with meaningful hand motion.

namespace
{
const char* kSkeletonPath = "assets/characters/humanoid/skeleton.ozz";
const char* kSwingPath = "assets/characters/humanoid/sword_and_shield_slash.ozz";
const char* kHips = "mixamorig:Hips";
const char* kRightHand = "mixamorig:RightHand";
} // namespace

TEST_CASE("computeJointReach happy path: swing clip + right hand", "[anim][reach]")
{
    const Skeleton skel = loadSkeleton(kSkeletonPath);
    REQUIRE(skel.isLoaded());
    const AnimationClip clip = loadAnimationClip(kSwingPath);
    REQUIRE(clip.isLoaded());

    // Sweep the full clip. Right hand on a swing extends a meaningful
    // distance from the hips at some point in the clip -- we don't
    // pin the exact value (it depends on the clip's authoring) but
    // require a non-trivial reach and a non-zero sample count.
    const JointReachResult r =
        computeJointReach(clip, skel, kHips, kRightHand, 0.0f, clip.duration());
    REQUIRE(r.sample_count > 0);
    REQUIRE(r.xz_max_meters > 0.10f); // hand isn't at hips during a swing
    REQUIRE(r.xz_max_meters < 2.00f); // sanity ceiling -- humanoid arms don't reach 2m
    REQUIRE(r.xz_at_window_end_meters >= 0.0f);
}

TEST_CASE("computeJointReach returns empty on missing joint name", "[anim][reach]")
{
    const Skeleton skel = loadSkeleton(kSkeletonPath);
    REQUIRE(skel.isLoaded());
    const AnimationClip clip = loadAnimationClip(kSwingPath);
    REQUIRE(clip.isLoaded());

    const JointReachResult r =
        computeJointReach(clip, skel, kHips, "this_joint_does_not_exist", 0.0f, clip.duration());
    REQUIRE(r.sample_count == 0);
    REQUIRE(r.xz_max_meters == 0.0f);
}

TEST_CASE("computeJointReach returns empty on zero-duration window", "[anim][reach]")
{
    const Skeleton skel = loadSkeleton(kSkeletonPath);
    REQUIRE(skel.isLoaded());
    const AnimationClip clip = loadAnimationClip(kSwingPath);
    REQUIRE(clip.isLoaded());

    // Window end equals window start -> nothing to sweep.
    const JointReachResult r = computeJointReach(clip, skel, kHips, kRightHand, 0.25f, 0.25f);
    REQUIRE(r.sample_count == 0);
    REQUIRE(r.xz_max_meters == 0.0f);
}
