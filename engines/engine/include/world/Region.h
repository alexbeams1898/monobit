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
// Regions architecture.
//
// A Region is a discrete world space: its own local-coordinate origin, its
// own set of static physics bodies (terrain trimesh, architecture meshes,
// triggers), its own ambient/sky/audio. At most one region is "current" at
// any moment; transitions swap which region is active.
//
// Real-physics doctrine: regions never share collision space. Cross-region
// collisions are impossible by construction — when region A deactivates,
// the engine removes every Jolt body it owned before region B activates.
//
// The diamond invariants:
//   1. No body outlives its region (engine tracks handles per region).
//   2. Regions are DATA: a single JsonRegion class consumes region.json
//      from disk; no per-region C++. New circle = author a folder.
//   3. Transitions are deterministic + testable (state machine,
//      programmatically drivable, body counts auditable at each step).
// ---------------------------------------------------------------------------

// Opaque region identifier. 0 = invalid.
struct RegionId
{
    std::uint32_t id = 0;
    bool operator==(RegionId o) const
    {
        return id == o.id;
    }
    bool operator!=(RegionId o) const
    {
        return id != o.id;
    }
};
inline constexpr RegionId kInvalidRegion{0};

// Region kind: drives gameplay-side conventions that are uniform across
// the whole region (camera follow distance, ambient mix, sky/no-sky).
// Replaces the per-XZ-rect isIndoors() trick — a chapel-interior region
// is fully interior, the surface is fully exterior. No mixing within
// one region; if you need partial interior, that's a new region.
enum class RegionKind : std::uint8_t
{
    Exterior, // outdoor / sky-bearing / wider follow distance
    Interior, // indoor / no-sky / tighter follow distance
};

// Mode of a region transition.
enum class TransitionMode : std::uint8_t
{
    // Single-frame load swap. Fastest; suitable for teleports where
    // the player UI is already covering the screen (menu/inventory).
    Instant,
    // Fade-to-black, swap, fade-in. The fade duration sets the load
    // budget — async loading completes during the black frame.
    Fade,
    // Both regions coexist; old region is destroyed when player crosses
    // a designated "commit" volume. Used for visible elevator-style
    // transitions where the player sees both sides.
    Continuous,
};

// State machine for a transition in progress.
//
// LoadingTarget was deleted: per the preload-everything-before-main-menu
// doctrine (pillar 9), all regions' assets (.glb meshes, Jolt shapes,
// CPU vertex arrays) are eagerly preloaded at boot via
// JsonRegion::preloadAssets() inside loadAllRegionsPreload(). Body insertion
// is the only per-activation work and is sub-millisecond. There is
// nothing to "load" at transition time, so the LoadingTarget state
// never had any work to do -- it just polled a future that was always
// already-ready. Removing it eliminates the async polling overhead and
// makes the transition contract trivially zero-delay.
//
// Fade mode kept: a fade-to-black is still useful for cinematic /
// fast-travel / death transitions even when the underlying load is
// instant. The fade just plays for its declared duration as a visual
// effect, not as cover for loading.
enum class TransitionState : std::uint8_t
{
    Idle,       // no transition active
    FadingOut,  // playing fade-to-black on screen
    Committing, // swapping bodies on main thread (single frame)
    FadingIn,   // playing fade-in on screen
};

// Trigger volume: when a registered actor (player today, others later)
// enters this AABB, the engine queues a transition to `target_region`.
// Edge-triggered: fires once on the frame the actor enters; does not
// re-fire while inside.
struct RegionTrigger
{
    std::string id;         // unique within region; used for save/load
    glm::vec3 center{0.0f}; // world-space, in OWNER region's local coords
    glm::vec3 half_extents{0.0f};
    RegionId target = kInvalidRegion;
    // If preserve_player_pos is true, the player's CURRENT world
    // position carries across the transition unchanged — the region
    // swaps but the player doesn't move. This is the seamless
    // doorway pattern: both regions share world coordinates at the
    // door, walking through means region-swap with zero motion.
    // target_spawn_pos is ignored when this is true.
    //
    // If false (cinematic / fast-travel default), the player is
    // teleported to target_spawn_pos in the target region's local
    // coords.
    bool preserve_player_pos = false;
    glm::vec3 target_spawn_pos{0.0f}; // local coords of target region
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

// Per-character state that crosses region boundaries unchanged.
// (Things the player keeps when they walk through a door.)
struct PersistentRegionState
{
    // Camera orientation persists so transitions don't disorient.
    float camera_yaw = 0.0f;
    float camera_pitch = 0.0f;
    // Everything else (hp, stamina, inventory, equipment, anim state)
    // is owned by gameplay code and is naturally persistent — the
    // engine does not touch it during transitions.
};

// A Region declares what it contains and provides callbacks for the
// engine to register/unregister its contents. The diamond pattern:
// the engine provides a RegionActivationContext during onActivate(),
// the region calls context.addBody() for each Jolt body it creates;
// the engine remembers those handles. On deactivate, the engine
// removes them all — the region cannot leak.
class Region;

class RegionActivationContext
{
  public:
    explicit RegionActivationContext(Region& s) : region_ref(s)
    {
    }

    // Record a Jolt body this region owns. Engine removes on deactivate.
    void addBody(engine::physics::BodyHandle h);

    // Record multiple at once.
    void addBodies(const std::vector<engine::physics::BodyHandle>& hs);

    // Add a trigger declaration. Trigger's `center` is in this region's
    // local coords.
    void addTrigger(const RegionTrigger& t);

  private:
    Region& region_ref;
};

// Region base. JsonRegion (the only real subclass we ship) parses
// region.json and dispatches to context.addBody/addTrigger.
class Region
{
  public:
    explicit Region(std::string id, std::string name, RegionKind kind = RegionKind::Exterior)
        : region_id(std::move(id)), debug_name(std::move(name)), kind_val(kind)
    {
    }
    virtual ~Region() = default;

    const std::string& regionId() const
    {
        return region_id;
    }
    const std::string& debugName() const
    {
        return debug_name;
    }
    RegionKind kind() const
    {
        return kind_val;
    }

    // Called by the engine when this region becomes active. Subclass
    // registers all bodies + triggers via the context. Region's
    // local-coords origin is world-origin unless the subclass
    // applies an offset to its bodies/triggers.
    virtual void onActivate(RegionActivationContext& ctx) = 0;

    // Hook for subclasses to release runtime state (audio bed, ambient
    // colors, etc.). Engine handles body cleanup automatically based
    // on what context.addBody recorded — subclass does NOT remove
    // bodies itself.
    virtual void onDeactivate()
    {
    }

    // Called once at engine shutdown for every registered region.
    // Subclasses release pre-loaded GPU/CPU asset resources here
    // (resident-all-regions model: assets stay through the session
    // and are freed only at exit).
    virtual void onShutdown()
    {
    }

    // Engine-internal: append/inspect body handles. JsonRegion + Region
    // subclasses should not touch these directly; use the context.
    void engineAppendBody(engine::physics::BodyHandle h)
    {
        owned_bodies.push_back(h);
    }
    void engineAppendTrigger(const RegionTrigger& t)
    {
        trigger_list.push_back(t);
    }
    void engineClearOwnership()
    {
        owned_bodies.clear();
        trigger_list.clear();
    }
    const std::vector<engine::physics::BodyHandle>& ownedBodies() const
    {
        return owned_bodies;
    }
    const std::vector<RegionTrigger>& triggers() const
    {
        return trigger_list;
    }

  private:
    std::string region_id;
    std::string debug_name;
    RegionKind kind_val = RegionKind::Exterior;
    std::vector<engine::physics::BodyHandle> owned_bodies;
    std::vector<RegionTrigger> trigger_list;
};

// ---- RegionManager (engine-global singleton) -------------------------------

// Register a region with the manager. Takes ownership.
RegionId registerRegion(std::unique_ptr<Region> s);

// Look up by region_id string (the value of Region::regionId()).
RegionId findRegionId(const char* region_id);

// Activate a region immediately (synchronous, no fade). Used at engine
// boot for the initial region. Subsequent in-game transitions should
// use transitionToRegion.
void activateRegionImmediate(RegionId);

// Begin a transition to the target region. Returns false if a
// transition is already in progress. The transition state machine
// runs in tickRegionManager(); transitions complete asynchronously.
bool beginTransition(RegionId target, TransitionMode mode, bool preserve_player_pos,
                     glm::vec3 target_spawn_pos, bool override_yaw, float target_yaw,
                     float fade_duration_seconds);

// Per-frame tick: advances transition state machine, handles async
// loading, swaps bodies on commit, advances fades. Returns the
// current transition state (Idle when nothing's happening).
TransitionState tickRegionManager(float dt);

// Currently active region. In the multi-resident architecture (all
// regions' bodies + meshes are resident from boot for zero-delay
// seamless traversal), "current" specifically means:
//   * Whose triggers the trigger-check iterates this frame
//   * Where new-character spawn defaults apply
//   * Which region's region_id the debug chip shows
// It does NOT mean "the only region whose bodies are in physics" --
// every registered region's bodies are in Jolt at all times, so the
// player can cross seams without any body insertion/removal at the
// transition moment.
RegionId currentRegion();
Region* currentRegionPtr(); // may be null

// All registered regions (for F1 force-transition menu + the multi-
// region renderer, which iterates every region's meshes each frame
// instead of just the current one). Indices are stable across the
// session.
int regionCount();
RegionId regionAt(int idx);
Region* regionPtr(RegionId);

// Current transition state (for F1 diagnostics).
TransitionState transitionState();
float transitionFadeAlpha(); // 0..1 black overlay alpha
RegionId transitionTarget();

// Player overlap check: pass the player's current world position
// each frame; engine fires any matching trigger (queues a transition).
// Returns the trigger that fired (or nullptr).
const RegionTrigger* checkPlayerTriggers(const glm::vec3& player_pos);

// Persistent state that the engine should preserve across transitions
// (camera yaw/pitch). Gameplay state (HP, inventory) is preserved
// trivially by NOT being region-local — it lives in gameplay code that
// the engine doesn't touch.
void setPersistentState(const PersistentRegionState& s);
const PersistentRegionState& persistentState();

// Post-commit callback: invoked by the manager on the frame the new
// region becomes active.
//   preserve_pos: if true, the player's current world position
//     should be kept (no teleport). spawn_pos is ignored.
//   override_yaw: if true, set player yaw to spawn_yaw; else leave
//     yaw alone.
using PostCommitCallback = void (*)(bool preserve_pos, const glm::vec3& spawn_pos,
                                    bool override_yaw, float spawn_yaw);
void setPostCommitCallback(PostCommitCallback cb);

// Engine bootstrap / teardown.
void initRegionManager();
void shutdownRegionManager();

} // namespace engine::world
