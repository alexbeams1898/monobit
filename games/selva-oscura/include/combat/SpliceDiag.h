#pragma once

#include "anim/AnimationClip.h"
#include "anim/ClipRegistry.h"
#include "anim/PoseSampler.h"

#include <glm/vec3.hpp>

#include <string>

namespace selva::combat
{

// Five-joint splice diagnostic. Captured pre-playOneShot, logged post.
// Lets fire paths (attack, block, dodge) emit identical columns when
// debugging splice quality.
struct SpliceDiag
{
    int rh = -1, lh = -1, hp = -1, lf = -1, rf = -1;
    glm::vec3 live_rh{}, live_lh{}, live_hp{}, live_lf{}, live_rf{};
};

SpliceDiag captureSpliceDiag(const selva::anim::PoseSampler& sampler);

// Sample each captured joint at `start_seconds` of `new_clip` and log
// the joint-by-joint distance from live to entry.
void logSpliceDiag(const SpliceDiag& d, const selva::anim::AnimationClip& new_clip,
                   float start_seconds, const char* prefix,
                   const selva::anim::PoseSampler& sampler);

// Pose-match scan against the live locomotion track. Returns the
// clip-time of the new clip whose joint world positions best match
// the loco track's current pose. Falls back to 0 if joints / clips
// unresolved.
float poseMatchStartFromLoco(const selva::anim::AnimationClip& new_clip, float window_seconds,
                             const selva::anim::PoseSampler& sampler,
                             const selva::anim::ClipRegistry& clips,
                             const std::string& last_loco_clip_name);

// True when the locomotion track is currently a moving (gait) clip
// rather than a stance idle.
bool isMovingLocoClip(const std::string& name);

// Cache the right-hand world position. Read by the next-frame fire-
// diagnostic to compute the live hand's velocity (this frame minus
// last frame).
void cacheRightHandPos(const selva::anim::PoseSampler& sampler);
glm::vec3 lastRightHandPos();
bool lastRightHandValid();

} // namespace selva::combat
