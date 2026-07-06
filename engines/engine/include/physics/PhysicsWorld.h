#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace engine::physics
{

// Identifier for a physics body. Opaque handle the gameplay code carries
// around. Internally maps to a Jolt BodyID or CharacterVirtual pointer.
// kInvalid means "no body" / "uninitialized."
struct BodyHandle
{
    std::uint32_t id = 0;
    bool operator==(BodyHandle o) const
    {
        return id == o.id;
    }
    bool operator!=(BodyHandle o) const
    {
        return id != o.id;
    }
};
inline constexpr BodyHandle kInvalidBody{0};

// Tag describing what kind of surface a body represents. Used by
// gameplay queries (footstep banks, indoor detection, etc.) so the
// physics layer stays domain-agnostic.
enum class SurfaceTag : std::uint8_t
{
    Unknown = 0,
    Terrain,      // outdoor heightmap surface
    Architecture, // chapel walls, plinth, descent, etc. (static)
    Foliage,      // trees, vegetation
    Actor,        // player / NPC capsule
};

// Hit result of a raycast.
struct RayHit
{
    bool hit = false;
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    BodyHandle body = kInvalidBody;
    SurfaceTag tag = SurfaceTag::Unknown;
};

// Bring up Jolt. Idempotent — calling twice returns false the second
// time. Engine.cpp calls this from its init path.
bool initPhysics();
void shutdownPhysics();

// Step the physics world by `dt` seconds. Caller (per-frame gameplay
// loop) drives this with the real frame delta. Multiple sub-steps are
// performed internally for stability if `dt` is large.
void updatePhysics(float dt);

// --- Static body builders ---

// Add a static trimesh body. Positions are world-space vertices;
// indices are triangle indices (3 per tri). `tag` propagates to
// raycast hits / ground queries so gameplay can identify the surface.
// `debug_name` is for diagnostic logging only (overlay labels,
// "stuck against X" messages); pass nullptr to skip.
// Returns kInvalidBody on failure.
//
// One-shot convenience: builds the shape AND adds the body. For
// hot-path activations (scene swap), use the split API below:
// preload shapes at boot, then add bodies on activation.
BodyHandle addStaticTrimesh(const std::vector<glm::vec3>& positions,
                            const std::vector<std::uint32_t>& indices, SurfaceTag tag,
                            const char* debug_name = nullptr);

// Opaque handle to a preloaded trimesh shape. Built once (slow:
// BVH construction can take 100s of ms for large meshes in Debug),
// then re-used to add bodies in O(1).
struct ShapeHandle
{
    std::uint32_t id = 0;
    bool operator==(ShapeHandle o) const
    {
        return id == o.id;
    }
    bool operator!=(ShapeHandle o) const
    {
        return id != o.id;
    }
};
inline constexpr ShapeHandle kInvalidShape{0};

// Build a trimesh shape (slow, BVH construction). The result is
// kept alive in an engine-side cache until shutdown. Call this at
// boot / load-time, NOT during per-frame or scene-transition code.
ShapeHandle createStaticTrimeshShape(const std::vector<glm::vec3>& positions,
                                     const std::vector<std::uint32_t>& indices);

// Add a static body backed by a previously-created shape. Fast
// (microseconds): no shape construction. The shape stays cached
// for reuse; this just registers a new body referencing it.
BodyHandle addStaticBodyFromShape(ShapeHandle shape, SurfaceTag tag,
                                  const char* debug_name = nullptr);

// Add a static axis-aligned box. `center` is the world-space center;
// `half_extents` are the half-widths along X/Y/Z.
BodyHandle addStaticBox(const glm::vec3& center, const glm::vec3& half_extents, SurfaceTag tag,
                        const char* debug_name = nullptr);

// Lookup the debug name for a body (returns "" if unknown).
const char* bodyDebugName(BodyHandle body);

// Remove any body (static or dynamic) and free its shape. Safe on
// kInvalidBody.
void removeBody(BodyHandle body);

// --- Character controller ---

// How a character collides with other characters. Solid is the
// default — pushes and is pushed by the player and other actors,
// like every normal creature. Incorporeal still stands on static
// terrain (gravity, ground snap, hazards all work) but passes
// through other characters. Used for actors that share space with
// the player: piles of larvae the player runs through, ghost / soul
// forms, incorporeal NPCs.
enum class CharacterCollision : std::uint8_t
{
    Solid = 0,
    Incorporeal = 1,
};

// Add a kinematic character (player or NPC). Uses Jolt's
// CharacterVirtual under the hood with built-in step-up + slope
// handling. `radius` is XZ capsule radius, `height` is total capsule
// height (cylinder portion + caps). `collision` picks which layer
// the character occupies — see CharacterCollision above.
BodyHandle addCharacter(const glm::vec3& position, float radius, float height,
                        CharacterCollision collision = CharacterCollision::Solid);

// Set the character's intended horizontal velocity (m/s, world XZ).
// Y is driven by gravity inside the controller; passing a Y here is
// ignored unless `apply_y` is true (used for jump impulses later).
void setCharacterVelocity(BodyHandle character, const glm::vec3& velocity_xz, bool apply_y = false);

// Read the character's current position (XYZ world).
glm::vec3 characterPosition(BodyHandle character);

// True if the character has firm contact with a walkable surface
// this frame.
bool isCharacterOnGround(BodyHandle character);

// Body the character is currently standing on (the "ground" body
// from Jolt's CharacterVirtual). kInvalidBody if not on ground.
BodyHandle characterGroundBody(BodyHandle character);

// All bodies the character is currently in contact with this frame
// (touching but not necessarily supporting). Useful for diagnosing
// "what wall am I jammed against?"
void characterActiveContacts(BodyHandle character, std::vector<BodyHandle>& out);

// Detailed contact info per active contact for the character. Each
// entry: which body, contact point in world space, contact normal,
// and (if available) the triangle's three vertex positions for
// trimesh contacts. tri_v0/v1/v2 are zeroed if not a trimesh contact.
struct CharacterContactDetail
{
    BodyHandle body;
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tri_v0;
    glm::vec3 tri_v1;
    glm::vec3 tri_v2;
    bool is_trimesh_triangle = false;
};
void characterActiveContactDetails(BodyHandle character, std::vector<CharacterContactDetail>& out);

// Teleport a character (resets velocity, no collision sweep). Used
// for spawn / scene load.
void teleportCharacter(BodyHandle character, const glm::vec3& position);

// --- Queries ---

// Cast a ray. Returns the nearest hit (or kNoHit) within max_distance.
// Excludes `ignore` body (e.g. don't self-hit the player capsule).
RayHit raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
               BodyHandle ignore = kInvalidBody);

// True if a sphere at `center` with `radius` overlaps any static body
// in the active scene. Used by the camera pull-in pipeline for
// perpendicular-wall detection that a forward raycast can miss.
// Character bodies are excluded so the camera doesn't "see" the player
// capsule.
bool sphereOverlap(const glm::vec3& center, float radius);

// Lookup the surface tag for a given body. Returns Unknown if
// the body is invalid or unknown.
SurfaceTag bodySurfaceTag(BodyHandle body);

// --- Debug enumeration ---

enum class BodyKind : std::uint8_t
{
    StaticTrimesh,
    StaticBox,
    Character,
};

struct BodyDebugInfo
{
    BodyHandle body;
    BodyKind kind;
    SurfaceTag tag;
    glm::vec3 world_aabb_min;
    glm::vec3 world_aabb_max;
    // Character-only: capsule center + half-height (cylinder portion)
    // + radius. Drawn as a capsule wireframe. Static bodies leave these
    // zero.
    glm::vec3 capsule_center{0.0f};
    float capsule_radius = 0.0f;
    float capsule_half_height = 0.0f;
};

// Enumerate every registered body (static + character) with its
// world-space AABB + kind + tag. Out vector is overwritten. Cheap —
// just walks the handle table; intended for debug overlays.
void enumerateBodies(std::vector<BodyDebugInfo>& out);

} // namespace engine::physics
