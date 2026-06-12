#include "world/Door.h"

#include "AppStateGlobal.h"
#include "audio/Audio.h"
#include "lang/Language.h"
#include "physics/PhysicsWorld.h"
#include "world/StaticMeshAssets.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdio>

namespace selva::world
{

const char* doorStateName(DoorState s)
{
    switch (s)
    {
    case DoorState::Locked:
        return "Locked";
    case DoorState::Closed:
        return "Closed";
    case DoorState::Opening:
        return "Opening";
    case DoorState::Open:
        return "Open";
    }
    return "?";
}

DoorState parseDoorState(const std::string& s)
{
    if (s == "Locked")
        return DoorState::Locked;
    if (s == "Closed")
        return DoorState::Closed;
    if (s == "Opening")
        return DoorState::Open; // promote interrupted Opening -> Open on load
    if (s == "Open")
        return DoorState::Open;
    return DoorState::Closed; // legacy / unknown -> Closed
}

namespace
{
std::vector<Door> sDoors;

// Look up the persisted state of a door from the active profile, if
// any. Returns nullptr if no entry / no profile -- caller falls back
// to the JSON-authored initial_state.
const std::string* persistedStateFor(const std::string& door_id)
{
    const auto* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return nullptr;
    for (const auto& pair : profile->door_states)
    {
        if (pair.first == door_id)
            return &pair.second;
    }
    return nullptr;
}

// Write (or update) a door's persisted state on the active profile.
// No-op if no profile or door isn't marked persistent.
void writePersistedState(const Door& door)
{
    if (!door.persistent)
        return;
    auto* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return;
    const char* name = doorStateName(door.state);
    for (auto& pair : profile->door_states)
    {
        if (pair.first == door.id)
        {
            pair.second = name;
            return;
        }
    }
    profile->door_states.emplace_back(door.id, std::string(name));
}

// Build / destroy the static collider for the door's CURRENT closed
// position. Used when entering Locked/Closed/Opening (collider needs
// to exist) and when entering Open (collider needs to be torn down).
// Collider is an axis-aligned box centered on door.pos sized for the
// chapel door cutout (1.0m wide x 2.15m tall x 0.1m deep). Future:
// per-door dimensions read from the mesh bounds.
constexpr float kDoorColliderWidth = 1.0f;
constexpr float kDoorColliderHeight = 2.15f;
constexpr float kDoorColliderDepth = 0.1f;

void ensureCollider(Door& door)
{
    if (door.collider != engine::physics::kInvalidBody)
        return;
    const glm::vec3 half(kDoorColliderWidth * 0.5f, kDoorColliderHeight * 0.5f,
                         kDoorColliderDepth * 0.5f);
    const glm::vec3 center(door.pos.x, door.pos.y + kDoorColliderHeight * 0.5f, door.pos.z);
    door.collider = engine::physics::addStaticBox(
        center, half, engine::physics::SurfaceTag::Architecture, door.id.c_str());
}

void removeCollider(Door& door)
{
    if (door.collider == engine::physics::kInvalidBody)
        return;
    engine::physics::removeBody(door.collider);
    door.collider = engine::physics::kInvalidBody;
}

void applyStateSideEffects(Door& door)
{
    // Locked + Closed + Opening = solid collision. Open = no collision.
    if (door.state == DoorState::Open)
        removeCollider(door);
    else
        ensureCollider(door);
}

} // namespace

std::vector<Door>& doors()
{
    return sDoors;
}

Door* findDoor(const std::string& id)
{
    for (auto& d : sDoors)
    {
        if (d.id == id)
            return &d;
    }
    return nullptr;
}

bool openDoor(const std::string& id)
{
    Door* d = findDoor(id);
    if (d == nullptr)
        return false;
    if (d->state == DoorState::Opening || d->state == DoorState::Open)
        return false;
    // Locked unlocks AND opens in one call (scripted opens bypass the
    // "unlock first, then open" distinction; player-interaction never
    // hits Locked because the prompt is gated).
    d->state = DoorState::Opening;
    d->opening_elapsed = 0.0f;
    writePersistedState(*d);
    std::fprintf(stderr, "[door] '%s': -> Opening\n", d->id.c_str());
    std::fflush(stderr);
    selva::audio::playSfx("door_open");
    return true;
}

bool unlockDoor(const std::string& id)
{
    Door* d = findDoor(id);
    if (d == nullptr)
        return false;
    if (d->state != DoorState::Locked)
        return false;
    d->state = DoorState::Closed;
    writePersistedState(*d);
    std::fprintf(stderr, "[door] '%s': Locked -> Closed\n", d->id.c_str());
    std::fflush(stderr);
    selva::audio::playSfx("door_lock");
    return true;
}

void tickDoors(float dt)
{
    for (auto& d : sDoors)
    {
        if (d.state != DoorState::Opening)
            continue;
        d.opening_elapsed += dt;
        if (d.opening_elapsed >= d.open_animation_seconds)
        {
            d.state = DoorState::Open;
            d.opening_elapsed = d.open_animation_seconds;
            applyStateSideEffects(d);
            writePersistedState(d);
            std::fprintf(stderr, "[door] '%s': Opening -> Open (collider removed)\n", d.id.c_str());
            std::fflush(stderr);
        }
    }
}

void registerDoorsForRegion(const std::vector<DoorDecl>& decls)
{
    sDoors.reserve(sDoors.size() + decls.size());
    for (const auto& decl : decls)
    {
        Door d;
        d.id = decl.id;
        d.pos = decl.pos;
        d.yaw = decl.yaw;
        d.hinge_offset = decl.hinge_offset;
        d.hinge_axis = decl.hinge_axis;
        d.open_angle_radians = decl.open_angle_radians;
        d.open_animation_seconds = decl.open_animation_seconds;
        d.mesh_path = decl.mesh_path;
        d.player_interactable = decl.player_interactable;
        d.persistent = decl.persistent;
        d.initial_state = decl.initial_state;
        d.state = decl.initial_state;
        d.label_key = decl.label_key;
        if (decl.persistent)
        {
            if (const std::string* persisted = persistedStateFor(decl.id))
                d.state = parseDoorState(*persisted);
        }
        applyStateSideEffects(d);
        // Load mesh in LOCAL space (world_origin = 0). Per-frame model
        // matrix in renderDoors() positions + rotates each door.
        if (!d.mesh_path.empty())
        {
            if (!loadStaticMesh(d.mesh_path.c_str(), glm::vec3(0.0f), d.visual_mesh))
            {
                std::fprintf(stderr, "[door] '%s' mesh load failed for '%s'\n", d.id.c_str(),
                             d.mesh_path.c_str());
                std::fflush(stderr);
            }
        }
        std::fprintf(stderr,
                     "[door] registered '%s' at (%.2f,%.2f,%.2f) initial=%s mesh=%zu prims\n",
                     d.id.c_str(), d.pos.x, d.pos.y, d.pos.z, doorStateName(d.state),
                     d.visual_mesh.primitives.size());
        std::fflush(stderr);
        sDoors.push_back(std::move(d));
    }
    // Second pass: register interactables for player-interactable doors.
    // Done AFTER the doors are pushed so the closures can capture by id
    // and resolve to a stable Door* via findDoor() each frame -- safer
    // than capturing by index across vector growth. The available()
    // closure live-gates the prompt on door state, so a single
    // registration covers the entire Locked->Closed->Opening->Open
    // lifecycle (Open hides the prompt; Locked hides the prompt).
    for (auto& d : sDoors)
    {
        if (!d.player_interactable)
            continue;
        if (d.interactable_id != selva::interact::kInvalidId)
            continue; // already registered (re-entry through region reload)
        const std::string door_id = d.id;
        selva::interact::Decl idecl;
        idecl.kind = selva::interact::Kind::Open;
        idecl.position = [door_id]()
        {
            const Door* dd = findDoor(door_id);
            return dd != nullptr ? dd->pos : glm::vec3(0.0f);
        };
        idecl.range_meters = 2.5f;
        // Label resolves through the language map. Authoring fallback:
        // if a player-interactable door has no label_key, log loudly
        // and use the door id (designer identifier; not player-facing
        // but better than empty). Per the doctrine, every authored
        // player-interactable door MUST declare a label_key.
        if (d.label_key.empty())
        {
            std::fprintf(stderr,
                         "[door] '%s' is player_interactable but has no label_key -- "
                         "falling back to door id (player will see the identifier!)\n",
                         d.id.c_str());
            std::fflush(stderr);
            idecl.label = door_id;
        }
        else
        {
            idecl.label = selva::lang::resolve(d.label_key);
        }
        idecl.on_interact = [door_id]() { openDoor(door_id); };
        idecl.available = [door_id]()
        {
            const Door* dd = findDoor(door_id);
            // Closed = unlocked + not yet opened. Only state where the
            // E-press is meaningful. Locked, Opening, Open all suppress.
            return dd != nullptr && dd->state == DoorState::Closed;
        };
        d.interactable_id = selva::interact::registerInteractable(std::move(idecl));
    }
}

void applyPersistedDoorStates()
{
    for (auto& d : sDoors)
    {
        if (!d.persistent)
            continue;
        const std::string* persisted = persistedStateFor(d.id);
        // Authoritative both directions: profile-entry wins if present;
        // otherwise revert to JSON-default initial_state. The latter
        // clears cross-character state when entering Playing on a
        // fresh save.
        const DoorState target =
            (persisted != nullptr) ? parseDoorState(*persisted) : d.initial_state;
        if (target == d.state)
            continue;
        std::fprintf(stderr, "[door] '%s' state %s -> %s (profile reconcile)\n", d.id.c_str(),
                     doorStateName(d.state), doorStateName(target));
        std::fflush(stderr);
        d.state = target;
        d.opening_elapsed = (target == DoorState::Open) ? d.open_animation_seconds : 0.0f;
        applyStateSideEffects(d);
    }
}

void removeDoorsForRegion()
{
    for (auto& d : sDoors)
    {
        if (d.interactable_id != selva::interact::kInvalidId)
        {
            selva::interact::unregisterInteractable(d.interactable_id);
            d.interactable_id = selva::interact::kInvalidId;
        }
        removeCollider(d);
        freeStaticMeshGLResources(d.visual_mesh);
    }
    sDoors.clear();
}

glm::mat4 doorModelMatrix(const Door& door)
{
    const float opening_frac =
        door.open_animation_seconds > 0.0f
            ? std::clamp(door.opening_elapsed / door.open_animation_seconds, 0.0f, 1.0f)
            : 1.0f;
    const float current_angle = (door.state == DoorState::Open) ? door.open_angle_radians
                                : (door.state == DoorState::Opening)
                                    ? door.open_angle_radians * opening_frac
                                    : 0.0f;
    // Translate to hinge in world, rotate around hinge_axis by yaw and
    // current_angle, then translate back. hinge_offset is local; the
    // door's local origin is already AT the hinge per gen_chapel_door.py,
    // so hinge_offset is typically zero for that door but kept for
    // future doors whose mesh origin sits elsewhere on the slab.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), door.pos);
    m = glm::rotate(m, door.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::vec3 axis(0.0f, 1.0f, 0.0f);
    if (door.hinge_axis == 'X')
        axis = glm::vec3(1.0f, 0.0f, 0.0f);
    else if (door.hinge_axis == 'Z')
        axis = glm::vec3(0.0f, 0.0f, 1.0f);
    m = glm::translate(m, door.hinge_offset);
    m = glm::rotate(m, current_angle, axis);
    m = glm::translate(m, -door.hinge_offset);
    return m;
}

} // namespace selva::world
