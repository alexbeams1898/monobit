#pragma once

#include "interact/Interaction.h"
#include "physics/PhysicsWorld.h"
#include "world/StaticMeshAssets.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace selva::world
{

// ER-style door world-object. State machine + persistent state +
// scripted-OR-player open. Once Open, terminal -- no closing, no
// re-locking. Each door instance is authored in a region's "doors":[]
// array; loaded at region activation; ticked in selvaPerFrame.
//
// Distinct from:
//   - static_meshes (those are inert; doors animate)
//   - triggers (those fire on player overlap; doors are interacted with)
//   - enemy_spawns (those are actors; doors don't move, don't have HP)
//
// Doors are load-bearing world-state objects, the chapel door first
// among them.
enum class DoorState : std::uint8_t
{
    Locked,  // Sealed; no interaction; collision solid; visual closed
    Closed,  // Closed but unlocked; player E-to-open if interactable; collision solid
    Opening, // Animating open; collision still solid; visual rotating
    Open,    // Terminal. No interaction. No collision. Visual fully open.
};

const char* doorStateName(DoorState s);
DoorState parseDoorState(const std::string& s);

struct Door
{
    std::string id;
    // World transform of the door's closed position. Hinge_offset is
    // relative to this; the hinge axis pivots through (pos +
    // hinge_offset).
    glm::vec3 pos;
    float yaw = 0.0f;
    // Where the hinge sits relative to pos (typically one edge of the
    // door slab). E.g. door width 1.0m, hinge_offset.x = -0.5 puts the
    // hinge at the -X edge of the door; the door rotates around that
    // axis.
    glm::vec3 hinge_offset{0.0f};
    // Which local axis the door rotates around. 'Y' = standard vertical
    // door hinge (swings horizontally). 'X' and 'Z' for trap doors /
    // portcullises / etc.
    char hinge_axis = 'Y';
    float open_angle_radians = 0.0f;
    float open_animation_seconds = 1.0f;
    // Mesh path -- procedurally generated for the chapel door; could
    // be authored per-region for future doors. Loaded at region
    // activation into mesh_gpu.
    std::string mesh_path;
    // Player can press E to open (only effective from Closed; Locked
    // shows no prompt or "Sealed" message).
    bool player_interactable = true;
    // Survive save/load via profile.door_states.
    bool persistent = true;
    // -- runtime fields below --
    DoorState state = DoorState::Closed;
    // JSON-authored default state. Used by applyPersistedDoorStates
    // to revert to baseline when the active profile has no entry for
    // this door (fresh character, or this door not yet touched).
    DoorState initial_state = DoorState::Closed;
    float opening_elapsed = 0.0f;
    // Static physics body for collision. Live when Locked/Closed/
    // Opening; removed when Open.
    engine::physics::BodyHandle collider = engine::physics::kInvalidBody;
    // Mesh in LOCAL space (vertices around the hinge origin). The
    // renderer applies a per-frame model matrix built from pos + yaw +
    // current opening rotation. Empty if mesh_path was missing or
    // failed to load.
    StaticMesh visual_mesh;
    // Interactable registry id for the "[E] Open {label}" prompt. Set
    // when registerDoorsForRegion installs the door; live state-gating
    // happens in the interactable's available() closure (only fires
    // the prompt when state == Closed && player_interactable). Cleared
    // on removeDoorsForRegion.
    selva::interact::Id interactable_id = selva::interact::kInvalidId;
    // Language-map key the interactable label resolves through. Copied
    // from the DoorDecl at registration. Empty for non-player-interactable
    // doors.
    std::string label_key;
};

// Per-frame model matrix for a door: world transform * current opening
// rotation around its hinge axis. Read by the renderer for both the
// color pass and the depth pass.
glm::mat4 doorModelMatrix(const Door& door);

// Process-wide door registry. Loaded by region activation; queried by
// game code via findDoor() / openDoor() / unlockDoor().
std::vector<Door>& doors();

// Resolve a door by id. Returns nullptr if no door with that id is
// loaded.
Door* findDoor(const std::string& id);

// State transitions. Funnel through these -- never write Door.state
// directly from callers. Returns true if the transition was made.
//   openDoor:    Locked -> Opening (also unlocks) OR Closed -> Opening
//   unlockDoor:  Locked -> Closed (no animation, no opening yet)
// Open and Opening are terminal-ish; openDoor on an Opening or Open
// door is a no-op returning false.
bool openDoor(const std::string& id);
bool unlockDoor(const std::string& id);

// Per-frame: advance Opening doors, transition to Open when animation
// completes, manage collision toggle. Called from selvaPerFrame after
// gameplay tick.
void tickDoors(float dt);

// Region activation hooks. Region loader calls registerDoorsForRegion
// at activate-time with the parsed door decls; deactivate path calls
// removeDoorsForRegion to unload colliders + mesh data.
struct DoorDecl
{
    std::string id;
    glm::vec3 pos;
    float yaw;
    glm::vec3 hinge_offset;
    char hinge_axis;
    float open_angle_radians;
    float open_animation_seconds;
    std::string mesh_path;
    bool player_interactable;
    bool persistent;
    DoorState initial_state;
    // Language-map key for the "[E] Open ..." prompt label. Resolved
    // each frame via selva::lang::resolve(). Empty = fall back to the
    // door id (designer identifier, NOT player-facing -- only OK if
    // the door isn't player_interactable).
    std::string label_key;
};
void registerDoorsForRegion(const std::vector<DoorDecl>& decls);
void removeDoorsForRegion(); // teardown on region deactivate (multi-region not yet supported;
                             // clears all)

// Re-read persisted door states from the active profile. Call after
// profile activation (load-game / new-game flow) -- doors registered
// at boot time saw a null profile and fell back to JSON initial_state.
void applyPersistedDoorStates();

} // namespace selva::world
