#pragma once

#include <string>
#include <vector>

namespace selva::anim
{

// One lockon anchor on an actor. List authored per-archetype (wolf
// has head/torso/hindleg_*) or per-skeleton as the fallback for
// archetypes that don't author (humanoid shades: a single "chest"
// point). Player cycles between points on the same target via the
// lockon-cycle input.
struct LockOnPointDecl
{
    std::string id;          // stable label ("head", "torso", "hindleg_left")
    std::string joint;       // joint name in the skeleton, resolved via PoseSampler::findJoint
    bool is_default = false; // first acquire snaps to this point; one entry should set true
};

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
    // Head -- appearance deformation anchor (per-bone head_scale
    // applied as a post-pass on the bone palette in
    // applyAppearanceDeformation). Empty = no head joint on this
    // rig; deformation is a no-op for this skeleton.
    std::string head;
    // Upper-arm roots (shoulder joints). Appearance deformation
    // anchor for arm_scale: the scale applies to these joints AND
    // recursively to their descendants (elbow + wrist + fingers),
    // so the whole arm chain grows/shrinks uniformly. Symmetric per
    // limb-design lock 2026-06-17: one arm_scale value drives both
    // sides. Empty = no arm deformation for this rig.
    std::string uparm_left;
    std::string uparm_right;
    // Torso root (spine base). Appearance deformation anchor for
    // torso_scale: scale propagates to the entire upper-body
    // subtree (chest, neck, head, shoulders, arms) so a broader
    // torso naturally broadens shoulders + raises head. Empty = no
    // torso deformation for this rig.
    std::string torso;
    // Default lockon points for any actor on this skeleton whose
    // archetype doesn't author its own. Humanoid: one entry at
    // "mixamorig:Spine2" labeled "chest". Wolf: a default Lupa or
    // other wolf-archetype that hasn't authored points falls back to
    // these (e.g. just "chest" at "Torso2"). Lupa's wolf.json overrides
    // with 4 points (head/torso/hindleg_*) so the player can cycle.
    // Empty list = no lock-on support for this skeleton (reticle
    // doesn't draw, no crash).
    std::vector<LockOnPointDecl> default_lockon_points;
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
