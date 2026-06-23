#pragma once

#include <string>

namespace selva::anim
{

class Skeleton;
class SkeletalMesh;
class ClipRegistry;
class PoseSampler;
class LocomotionConfig;
class AnimationClip;

// Player skeleton id ("player" → the humanoid rig). All existing actors
// (player + every shade) share this rig today. Wolf adds key "wolf".
// Future custom rigs add their own keys.
constexpr const char* kPlayerSkeletonKey = "player";

// Singleton accessors for the PLAYER's rig + clip registry + pose
// sampler + locomotion config. Backward-compatible: every existing
// caller implicitly means the player. New non-humanoid actors use
// the keyed accessors below.
Skeleton& skeleton();
SkeletalMesh& playerMesh();
ClipRegistry& clips();
PoseSampler& sampler();
LocomotionConfig& locomotionConfig();

// Keyed accessors. Resolve via the multi-skeleton registry built at
// boot from config/skeletons/<id>.json + per-skeleton bake outputs
// in assets/characters/<id>/. Returns the PLAYER's rig if the key
// isn't registered (graceful fallback — caller logged the miss).
Skeleton& skeletonByKey(const std::string& key);
SkeletalMesh& meshByKey(const std::string& key);
ClipRegistry& clipsByKey(const std::string& key);

// True if the key resolves to a real registered skeleton (not the
// player fallback). Use to detect typos / missing archetypes at
// spawn time.
bool hasSkeleton(const std::string& key);

const AnimationClip* idleClip();
const AnimationClip* walkClip();
const AnimationClip* runClip();

// Load every skeleton declared in config/skeletons/<id>.json, plus
// the player's rig (always loaded). Returns false on failure to
// load the PLAYER rig (other rigs failing is logged but non-fatal
// -- their archetypes just won't spawn).
bool initSkeletalAssets();
void shutdownSkeletalAssets();

// Phase-0 audit: print per-clip hip-XZ path length and per-clip
// trajectory for combat clips. Cheap one-time scan; meant to be
// called at startup to verify which clips ship with authored hip
// motion. Audits only the player's clips.
void auditClipHipMotion();

} // namespace selva::anim
