#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <vector>

// World collision — static cylinder colliders the player (and future
// enemies) push out of. Pillar-1 simple: a list of cylinders and a
// push-out resolution pass. The API hides the storage so the
// implementation can swap from linear scan to a spatial grid when
// scale demands it (~50+ colliders); call sites don't change.
//
// Scope: STATIC world geometry only (trees, rocks, fixed props).
// Dynamic actors (NPCs, knockables) get their own system when needed.
namespace selva::world
{

// One static cylinder collider. Plain floats so it's JSON-friendly
// when the time comes to load levels from disk. half_height is
// present but currently unused (XZ-only resolution); becomes
// load-bearing when Y motion / jumping over short props arrives.
struct CylinderCollider
{
    glm::vec3 center;         // base position (Y at ground level)
    float radius = 0.30f;     // XZ radius in meters
    float half_height = 2.0f; // half the cylinder's vertical extent
    // -1 = hash-driven variant pick (default for scattered trees).
    // >=0 = force a specific tree variant index. Used for authored
    // placements like the cypresses flanking the crypt.
    int forced_variant_idx = -1;
    float forced_scale = 0.0f;       // 0 = use hash-driven scale
    // Collision-only: skip in the tree renderer. Used for invisible
    // colliders that approximate non-cylindrical architecture (e.g.
    // the apse rear curve).
    bool collision_only = false;
};

// Axis-aligned box collider in XZ (no yaw). Walls of static
// architecture (the crypt's four walls, doorway-gap surrounds, etc.).
// Y is not collided against — actors are kept on the terrain by the
// renderer's height sampling.
struct BoxCollider
{
    glm::vec2 center;      // XZ center
    glm::vec2 half_extents;// XZ half-width / half-depth
};

// XZ rectangle marking where the player counts as "indoors" — used
// by surface-aware systems (footstep audio, future ambient/reverb)
// to switch behavior when the player enters enclosed architecture.
// Pure 2D: no Y semantics. Y comes from the terrain sample as usual.
struct InteriorFootprint
{
    glm::vec2 center;
    glm::vec2 half_extents;
};

// One named scene's worth of static colliders. Owned/loaded/unloaded
// as a unit so multiple scenes can coexist or swap. For now there's
// exactly one scene (the selva oscura hub), but the indirection means
// adding a second area later is data work, not refactor work.
//
// boundary_radius defines a circular play area centered at boundary_center
// (XZ); resolveBodyCollision pushes the body back inside if it tries to
// leave. Set boundary_radius <= 0 to disable.
struct CollisionScene
{
    std::vector<CylinderCollider> cylinders;
    std::vector<BoxCollider> boxes;
    std::vector<InteriorFootprint> interior_footprints;
    glm::vec2 boundary_center{0.0f, 0.0f};
    float boundary_radius = 0.0f;
};

// True if `body_xz` is inside any InteriorFootprint rectangle.
bool isIndoors(const glm::vec2& body_xz);

// Initialize the hub scene with its hardcoded cylinder set. Called
// once at startup. Replace with a JSON loader when manual editing
// becomes painful (~50+ cylinders).
void initHubScene();

// Read-only access for the renderer (drawing placeholder geometry
// at each cylinder) and for diagnostics.
const CollisionScene& currentScene();

// Resolve overlap: push body_xz radially out of any cylinder it
// penetrates. Body-agnostic — same call used by the player today,
// by enemies later. body_radius is the body's XZ capsule radius
// (~0.35m for a human).
//
// Runs multiple resolution passes so corner cases (player wedged
// between two adjacent cylinders) settle in one frame. 3 passes is
// enough for any non-pathological cylinder layout.
void resolveBodyCollision(glm::vec2& body_xz, float body_radius);

} // namespace selva::world
