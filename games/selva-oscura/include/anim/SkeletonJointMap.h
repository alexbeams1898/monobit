#pragma once

#include <string>

namespace selva::anim
{

// Per-skeleton named joint references. Decouples PoseSampler from
// Mixamo conventions: any skeleton (X_Bot, wolf, custom quadruped,
// custom four-armed monstrosity) provides its own joint-name map
// in config/skeletons/<id>.json. PoseSampler looks up by SEMANTIC
// role (hips, foot_left, etc.) and the map translates to the
// skeleton's actual joint name (which is then resolved to an
// index via findJointByName).
//
// All fields are joint NAMES, not indices. Empty string = this
// skeleton doesn't have that joint (e.g. a snake skeleton has no
// upleg_left). PoseSampler must tolerate empty / missing joints
// gracefully -- the feature that depends on the joint just no-ops
// for that skeleton.
struct SkeletonJointMap
{
    std::string skeleton_id;
    // Root motion + locomotion anchor.
    std::string hips;
    // Leg subtree roots -- used by buildUpperBodyWeights to mask
    // the legs out of upper-body one-shots so locomotion can drive
    // the legs independently.
    std::string upleg_left;
    std::string upleg_right;
    // Knees -- pose-match for blend-out (current to-loco landing
    // pose picks a frame whose leg geometry matches the closing
    // one-shot pose).
    std::string leg_left;
    std::string leg_right;
    // Feet -- IK anchor + pose-match.
    std::string foot_left;
    std::string foot_right;
};

// Load a skeleton joint map from JSON. Path is config/skeletons/<id>.json.
// Returns a map with skeleton_id set even on failure (caller checks for
// empty joint names to know if load succeeded). Missing file logs an
// error and returns a default-constructed map.
SkeletonJointMap loadSkeletonJointMap(const std::string& id);

// Per-skeleton accessor. Returns the player's map if the id isn't
// registered (graceful fallback; caller logs the miss).
const SkeletonJointMap& jointMapByKey(const std::string& key);

// Load all skeleton joint maps at boot. Called from
// initSkeletalAssets. Player map ("player") is required; others
// best-effort (their archetypes log a miss if absent).
void loadAllSkeletonJointMaps();

} // namespace selva::anim
