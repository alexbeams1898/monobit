#pragma once

namespace selva::anim
{

class Skeleton;
class SkeletalMesh;
class ClipRegistry;
class PoseSampler;
class LocomotionConfig;
class AnimationClip;

// Singleton accessors for the player rig + clip registry + pose sampler
// + locomotion config. All five live behind these accessors and are
// initialized by initSkeletalAssets(). Game code reads via these
// helpers; tests use loadDirectory directly.
Skeleton& skeleton();
SkeletalMesh& playerMesh();
ClipRegistry& clips();
PoseSampler& sampler();
LocomotionConfig& locomotionConfig();

const AnimationClip* idleClip();
const AnimationClip* walkClip();
const AnimationClip* runClip();

// Load skeleton, mesh, clip directory, and pre-warm the sampler. Returns
// false on any failure; main runs without skeletal rendering in that case.
bool initSkeletalAssets();
void shutdownSkeletalAssets();

// Phase-0 audit: print per-clip hip-XZ path length and per-clip
// trajectory for combat clips. Cheap one-time scan; meant to be
// called at startup to verify which clips ship with authored hip
// motion.
void auditClipHipMotion();

} // namespace selva::anim
