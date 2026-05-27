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
    float forced_scale = 0.0f; // 0 = use hash-driven scale
    // Collision-only: skip in the tree renderer. Used for invisible
    // colliders that approximate non-cylindrical architecture (e.g.
    // the apse rear curve).
    bool collision_only = false;
    // Optional name for the collider-debug overlay. Static string
    // literal (no allocation). nullptr = no label drawn.
    const char* name = nullptr;
};

// Axis-aligned box collider in XZ. Walls of static architecture
// (the crypt's four walls, doorway-gap surrounds, etc.).
//
// Player collision uses XZ only (actors are kept on the terrain by
// the renderer's height sampling). Camera collision is full 3D —
// y_base is the slab's bottom in world Y, half_height_y is the half
// vertical extent. Walls modeled as real 3D boxes so the camera can
// look up over a wall without the ray treating it as infinite-tall.
struct BoxCollider
{
    glm::vec2 center;           // XZ center
    glm::vec2 half_extents;     // XZ half-width / half-depth
    float y_base = 0.0f;        // bottom of the box in world Y
    float half_height_y = 5.0f; // half vertical extent (top = y_base + 2*half)
    // Camera-only: skip in resolveBodyCollision. Used for overhead
    // colliders (chapel roof slab) that the player walks UNDER but
    // the camera must not fly OVER. Player collision is XZ-only by
    // contract, so without this flag a roof's XZ footprint blocks
    // the player's walk-through path.
    bool camera_only = false;
    // Walkable top: the actor's ground Y at XZ inside this box is
    // the top of this box (y_base + 2*half_height_y), overriding
    // the terrain sample. Used for stair steps and raised platforms
    // that the player should stand on. Default false — walls,
    // headers, roofs all want the actor to read terrain Y normally,
    // not climb onto their top surface.
    bool walkable_top = false;
    // Top slope gradient (Y change per meter of XZ travel). Default
    // zero = flat. For ramps / corridor stair sections, set this so
    // groundHeight returns a Y that varies linearly across the box's
    // XZ footprint. The top Y at the center is y_base + 2*half_height_y;
    // top Y at (x, z) is center_top + dot(top_slope, (x-center, z-center)).
    glm::vec2 top_slope{0.0f, 0.0f};
    // Optional name for the collider-debug overlay. Static string
    // literal (no allocation). nullptr = no label drawn.
    const char* name = nullptr;
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

// Result of a ray-vs-scene query. `hit` is false if the ray reached
// `max_distance` clear; in that case `distance` is undefined.
struct RaycastHit
{
    float distance = 0.0f;
    bool hit = false;
};

// Cast a 3D ray against the static scene's cylinders + boxes and
// return the nearest hit (or no-hit) within `max_distance`.
//
// `direction` does not need to be unit-length but must be non-zero;
// it is normalized internally. `distance` on hit is in world units
// along `direction` from `origin`.
//
// Used by the camera to pull in when its ideal position would clip
// through a wall or tree. Pair with sphereOverlapsScene to enforce
// a buffer around the camera position after pull-in.
//
// Takes the scene explicitly so the math is testable against
// synthetic scenes without touching the singleton.
RaycastHit raycastScene(const CollisionScene& scene, const glm::vec3& origin,
                        const glm::vec3& direction, float max_distance);

// True if a sphere of `radius` centered at `center` overlaps any
// cylinder or box in the scene. Used to enforce the camera-clearance
// invariant: after pull-in places the camera somewhere, this query
// confirms the camera sphere isn't inside a wall. If it is, the
// caller shrinks the camera-to-player distance and re-queries until
// clear (the iterative push-out pass).
//
// Used in tandem with raycastScene: raycastScene catches occluders
// in the camera's forward path; sphereOverlapsScene catches the
// "candidate camera position lands inside a perpendicular wall"
// corner-pocket case that a single forward ray can't see.
bool sphereOverlapsScene(const CollisionScene& scene, const glm::vec3& center, float radius);

} // namespace selva::world
