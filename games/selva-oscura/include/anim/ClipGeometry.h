#pragma once

#include "anim/AnimationClip.h"
#include "anim/Skeleton.h"

namespace selva::anim
{

// Result of a reach scan over a clip + joint + window.
//
// One number matters for AI gating: `xz_max_meters`. The other
// fields are for diagnostics + tests.
//
// xz_max_meters: the maximum XZ-plane distance, in actor-local
// space, from the rig's hip joint to the target joint at any
// sampled time within the window. This is the "how far the
// strike geometry extends from the actor's center" answer the
// BT needs to decide chase-stop / fire-gate distances. Vertical
// (Y) extension is intentionally ignored -- a strike that lifts
// the foot up doesn't extend reach toward a target in front.
//
// xz_at_window_end_meters: the XZ distance at the LAST sample
// (clamped to window). Used by diagnostics to spot
// always-extending-but-never-recovering motion, and by tests
// to verify the sweep ran through the whole window.
//
// sample_count: number of valid samples taken. Zero means
// the clip / joint / skeleton couldn't be sampled -- caller
// should treat `xz_max_meters` as zero and fall back to a
// designer override or a safe default.
struct JointReachResult
{
    float xz_max_meters = 0.0f;
    float xz_at_window_end_meters = 0.0f;
    int sample_count = 0;
};

// Sweep `clip` from `window_start_seconds` to `window_end_seconds`
// at `sample_hz`. At each sample, compute the XZ distance, in
// actor-local space, from the rig's hip joint (named
// `hips_joint_name`) to the target joint (named `target_joint_name`).
// Track the maximum across the sweep.
//
// Returns a JointReachResult with sample_count=0 if any of:
//   - clip or skeleton isn't loaded
//   - either joint name is missing from the skeleton
//   - window_end_seconds <= window_start_seconds
//   - clip duration is zero
//   - sample_hz <= 0
//
// "Actor-local space" here is computed by subtracting the hip
// joint's model-space XZ from the target joint's model-space XZ
// at each sample. This is the rig-relative offset of the target
// joint in the XZ plane at that instant -- equivalent to the
// gameplay-relevant "how far does the joint extend from the
// actor's center" measurement. Yaw doesn't enter because both
// joints are in the same coordinate frame; the XZ delta is what
// AI gating cares about regardless of which way the actor is
// currently facing in the world.
//
// Sample rate default of 60 Hz matches the loco-splice pose-match
// pass -- low enough for fast clip-load, high enough to not miss
// brief peaks in a 0.1-0.5s active window.
JointReachResult computeJointReach(const AnimationClip& clip, const Skeleton& skeleton,
                                   const char* hips_joint_name, const char* target_joint_name,
                                   float window_start_seconds, float window_end_seconds,
                                   float sample_hz = 60.0f);

} // namespace selva::anim
