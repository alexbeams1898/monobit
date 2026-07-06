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

// Default player skeleton bundle. Body Type 1 in the character creator;
// the parametric pair humanoid_male / humanoid_female both ship today
// (per [[anim-intent-animset-architecture]]). Used as the fallback for
// any actor whose skeleton_id is empty (archetype JSONs should always
// specify). The PLAYER's active skeleton is resolved at runtime via
// playerSkeletonKey() reading sPlayer.appearance.body_type, so the
// accessors below (skeleton/playerMesh/clips) automatically swap
// based on the loaded character.
constexpr const char* kPlayerSkeletonKey = "humanoid_male";

// Runtime player-skeleton resolver. Returns the key set by
// setPlayerSkeletonKey() (called when the player's Appearance loads),
// or kPlayerSkeletonKey at boot before the player is constructed.
// Read by the no-arg accessors below + by PerFrameTick's AnimSet /
// joint-map lookups. Decoupled from gameplay/Actor.h to avoid an
// include cycle: gameplay sets the key when Appearance loads;
// SkeletalAssets reads it.
const char* playerSkeletonKey();
void setPlayerSkeletonKey(const char* key);

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

// Per-archetype mesh override, keyed by .glb path (relative to CWD
// at boot time -- usually "assets/characters/enemies/<id>/humanoid.glb").
// Follows the FromSoft pattern: every humanoid enemy archetype has
// its own baked mesh with its own skin diffuse, but shares the
// skeleton_id bundle's clip library for animation.
//
// The mesh's inverse-bind matrices MUST match the shared skeleton
// bundle's skeleton (same joint order + rest pose). Skeleton is
// picked up via the caller's skeleton_id lookup; only the mesh
// varies here.
//
// Loads on first call and caches. If the path is missing / fails to
// load, logs and returns the shared bundle's default mesh (via the
// provided fallback_skeleton_id) so gameplay keeps rendering.
SkeletalMesh& meshByArchetypePath(const std::string& path, const std::string& fallback_skeleton_id);

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
