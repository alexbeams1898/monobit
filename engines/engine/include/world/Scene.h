#pragma once

#include "physics/PhysicsWorld.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace engine::world
{

// ---------------------------------------------------------------------------
// Scenes architecture.
//
// A Scene is a discrete world space: its own local-coordinate origin, its
// own set of static physics bodies (terrain trimesh, architecture meshes,
// triggers), its own ambient/sky/audio. At most one scene is "current" at
// any moment; transitions swap which scene is active.
//
// Real-physics doctrine: scenes never share collision space. Cross-scene
// collisions are impossible by construction — when scene A deactivates,
// the engine removes every Jolt body it owned before scene B activates.
//
// The diamond invariants:
//   1. No body outlives its scene (engine tracks handles per scene).
//   2. Scenes are DATA: a single JsonScene class consumes scene.json
//      from disk; no per-scene C++. New circle = author a folder.
//   3. Transitions are deterministic + testable (state machine,
//      programmatically drivable, body counts auditable at each step).
// ---------------------------------------------------------------------------

// Opaque scene identifier. 0 = invalid.
struct SceneId
{
    std::uint32_t id = 0;
    bool operator==(SceneId o) const
    {
        return id == o.id;
    }
    bool operator!=(SceneId o) const
    {
        return id != o.id;
    }
};
inline constexpr SceneId kInvalidScene{0};

// Scene kind: drives gameplay-side conventions that are uniform across
// the whole scene (camera follow distance, ambient mix, sky/no-sky).
// Replaces the per-XZ-rect isIndoors() trick — a chapel-interior scene
// is fully interior, the surface is fully exterior. No mixing within
// one scene; if you need partial interior, that's a new scene.
enum class SceneKind : std::uint8_t
{
    Exterior, // outdoor / sky-bearing / wider follow distance
    Interior, // indoor / no-sky / tighter follow distance
};

// Mode of a scene transition.
enum class TransitionMode : std::uint8_t
{
    // Single-frame load swap. Fastest; suitable for teleports where
    // the player UI is already covering the screen (menu/inventory).
    Instant,
    // Fade-to-black, swap, fade-in. The fade duration sets the load
    // budget — async loading completes during the black frame.
    Fade,
    // Both scenes coexist; old scene is destroyed when player crosses
    // a designated "commit" volume. Used for visible elevator-style
    // transitions where the player sees both sides.
    Continuous,
};

// State machine for a transition in progress.
enum class TransitionState : std::uint8_t
{
    Idle,          // no transition active
    LoadingTarget, // worker thread loading target scene assets
    FadingOut,     // playing fade-to-black on screen
    Committing,    // swapping bodies on main thread (single frame)
    FadingIn,      // playing fade-in on screen
};

// Trigger volume: when a registered actor (player today, others later)
// enters this AABB, the engine queues a transition to `target_scene`.
// Edge-triggered: fires once on the frame the actor enters; does not
// re-fire while inside.
struct SceneTrigger
{
    std::string id;         // unique within scene; used for save/load
    glm::vec3 center{0.0f}; // world-space, in OWNER scene's local coords
    glm::vec3 half_extents{0.0f};
    SceneId target = kInvalidScene;
    // If preserve_player_pos is true, the player's CURRENT world
    // position carries across the transition unchanged — the scene
    // swaps but the player doesn't move. This is the seamless
    // doorway pattern: both scenes share world coordinates at the
    // door, walking through means scene-swap with zero motion.
    // target_spawn_pos is ignored when this is true.
    //
    // If false (cinematic / fast-travel default), the player is
    // teleported to target_spawn_pos in the target scene's local
    // coords.
    bool preserve_player_pos = false;
    glm::vec3 target_spawn_pos{0.0f}; // local coords of target scene
    // If override_yaw is true, the post-commit teleport sets the
    // player's yaw to target_yaw. If false, the player's previous
    // yaw is preserved across the transition (the seamless-traversal
    // default — walking through a door doesn't reorient you).
    bool override_yaw = false;
    float target_yaw = 0.0f;
    TransitionMode mode = TransitionMode::Fade;
    float fade_duration_seconds = 0.4f;
    std::string debug_name;
};

// Per-character state that crosses scene boundaries unchanged.
// (Things the player keeps when they walk through a door.)
struct PersistentSceneState
{
    // Camera orientation persists so transitions don't disorient.
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    // Everything else (hp, stamina, inventory, equipment, anim state)
    // is owned by gameplay code and is naturally persistent — the
    // engine does not touch it during transitions.
};

// A Scene declares what it contains and provides callbacks for the
// engine to register/unregister its contents. The diamond pattern:
// the engine provides a SceneActivationContext during onActivate(),
// the scene calls context.addBody() for each Jolt body it creates;
// the engine remembers those handles. On deactivate, the engine
// removes them all — the scene cannot leak.
class Scene;

class SceneActivationContext
{
  public:
    explicit SceneActivationContext(Scene& s) : mScene(s)
    {
    }

    // Record a Jolt body this scene owns. Engine removes on deactivate.
    void addBody(engine::physics::BodyHandle h);

    // Record multiple at once.
    void addBodies(const std::vector<engine::physics::BodyHandle>& hs);

    // Add a trigger declaration. Trigger's `center` is in this scene's
    // local coords.
    void addTrigger(const SceneTrigger& t);

  private:
    Scene& mScene;
};

// Scene base. JsonScene (the only real subclass we ship) parses
// scene.json and dispatches to context.addBody/addTrigger.
class Scene
{
  public:
    explicit Scene(std::string scene_id, std::string debug_name,
                   SceneKind kind = SceneKind::Exterior)
        : mSceneId(std::move(scene_id)), mDebugName(std::move(debug_name)), mKind(kind)
    {
    }
    virtual ~Scene() = default;

    const std::string& sceneId() const
    {
        return mSceneId;
    }
    const std::string& debugName() const
    {
        return mDebugName;
    }
    SceneKind kind() const
    {
        return mKind;
    }

    // Called by the engine when this scene becomes active. Subclass
    // registers all bodies + triggers via the context. Scene's
    // local-coords origin is world-origin unless the subclass
    // applies an offset to its bodies/triggers.
    virtual void onActivate(SceneActivationContext& ctx) = 0;

    // Hook for subclasses to release runtime state (audio bed, ambient
    // colors, etc.). Engine handles body cleanup automatically based
    // on what context.addBody recorded — subclass does NOT remove
    // bodies itself.
    virtual void onDeactivate()
    {
    }

    // Called once at engine shutdown for every registered scene.
    // Subclasses release pre-loaded GPU/CPU asset resources here
    // (resident-all-scenes model: assets stay through the session
    // and are freed only at exit).
    virtual void onShutdown()
    {
    }

    // Engine-internal: append/inspect body handles. JsonScene + Scene
    // subclasses should not touch these directly; use the context.
    void engineAppendBody(engine::physics::BodyHandle h)
    {
        mOwnedBodies.push_back(h);
    }
    void engineAppendTrigger(const SceneTrigger& t)
    {
        mTriggers.push_back(t);
    }
    void engineClearOwnership()
    {
        mOwnedBodies.clear();
        mTriggers.clear();
    }
    const std::vector<engine::physics::BodyHandle>& ownedBodies() const
    {
        return mOwnedBodies;
    }
    const std::vector<SceneTrigger>& triggers() const
    {
        return mTriggers;
    }

  private:
    std::string mSceneId;
    std::string mDebugName;
    SceneKind mKind = SceneKind::Exterior;
    std::vector<engine::physics::BodyHandle> mOwnedBodies;
    std::vector<SceneTrigger> mTriggers;
};

// ---- SceneManager (engine-global singleton) -------------------------------

// Register a scene with the manager. Takes ownership.
SceneId registerScene(std::unique_ptr<Scene> s);

// Look up by scene_id string (the value of Scene::sceneId()).
SceneId findSceneId(const char* scene_id);

// Activate a scene immediately (synchronous, no fade). Used at engine
// boot for the initial scene. Subsequent in-game transitions should
// use transitionToScene.
void activateSceneImmediate(SceneId);

// Begin a transition to the target scene. Returns false if a
// transition is already in progress. The transition state machine
// runs in tickSceneManager(); transitions complete asynchronously.
bool beginTransition(SceneId target, TransitionMode mode, bool preserve_player_pos,
                     glm::vec3 target_spawn_pos, bool override_yaw, float target_yaw,
                     float fade_duration_seconds);

// Per-frame tick: advances transition state machine, handles async
// loading, swaps bodies on commit, advances fades. Returns the
// current transition state (Idle when nothing's happening).
TransitionState tickSceneManager(float dt);

// Currently active scene (the one whose bodies are in Jolt right now).
SceneId currentScene();
Scene* currentScenePtr(); // may be null

// All registered scenes (for F1 force-transition menu).
int sceneCount();
SceneId sceneAt(int idx);
Scene* scenePtr(SceneId);

// Current transition state (for F1 diagnostics).
TransitionState transitionState();
float transitionFadeAlpha(); // 0..1 black overlay alpha
SceneId transitionTarget();

// Player overlap check: pass the player's current world position
// each frame; engine fires any matching trigger (queues a transition).
// Returns the trigger that fired (or nullptr).
const SceneTrigger* checkPlayerTriggers(const glm::vec3& player_pos);

// Persistent state that the engine should preserve across transitions
// (camera yaw/pitch). Gameplay state (HP, inventory) is preserved
// trivially by NOT being scene-local — it lives in gameplay code that
// the engine doesn't touch.
void setPersistentState(const PersistentSceneState& s);
const PersistentSceneState& persistentState();

// Post-commit callback: invoked by the manager on the frame the new
// scene becomes active.
//   preserve_pos: if true, the player's current world position
//     should be kept (no teleport). spawn_pos is ignored.
//   override_yaw: if true, set player yaw to spawn_yaw; else leave
//     yaw alone.
using PostCommitCallback = void (*)(bool preserve_pos, const glm::vec3& spawn_pos,
                                    bool override_yaw, float spawn_yaw);
void setPostCommitCallback(PostCommitCallback cb);

// Engine bootstrap / teardown.
void initSceneManager();
void shutdownSceneManager();

} // namespace engine::world
