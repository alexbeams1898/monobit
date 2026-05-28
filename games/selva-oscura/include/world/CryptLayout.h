#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Per-asset measurements for the entrance-to-Hell chapel. NOT a
// general indoor-space type — these are this one prop's specific
// dimensions. Defined in one place so the collider authoring
// (walls, apse arc, interior floor) and the render-side model
// matrix can't drift apart.
//
// When a second authored static-mesh asset lands (another chapel,
// a city wall, a gate), give it its own header next to this one
// rather than reusing this namespace. Each prop's measurements are
// its own data table. If MANY assets accumulate, promote the
// pattern to a JSON manifest keyed by asset name.

namespace selva::world
{
namespace crypt_layout
{
constexpr float kCryptX = 0.0f;
constexpr float kCryptZ = -210.0f;
constexpr float kHalfWidth = 3.0f;  // body_width/2 = 6m/2
constexpr float kHalfLength = 4.0f; // body_length/2 = 8m/2
constexpr float kWallThickness = 0.6f;
constexpr float kPlinthHeight = 0.30f;
constexpr float kDoorHalfWidth = 0.5f;
constexpr float kApseRadius = 1.8f;
// Wall vertical extent for camera raycast (not player collision; the
// player is XZ-only on the terrain). Sized to enclose the visible
// mesh's roofline so the camera can't rise up and over a wall through
// a gap that doesn't exist in the geometry.
constexpr float kWallHeight = 5.0f;
// Doorway opening height (top of the gap in the front wall). Above
// this, the front wall is solid in the visible mesh, so the camera
// header collider bridges Y in [kDoorHeight, kWallHeight].
constexpr float kDoorHeight = 2.2f;

// ---- Descent stair (must stay in sync with gen_crypt_foundation.py) ----
// Step proportions used by the visual mesh; collision treats each
// flight as a single sloped ramp (top_slope), not per-step boxes.
constexpr float kStairRise = 0.16f;
constexpr float kStairTread = 0.35f;

// Upper flights: two flanking, descend in chapel-local -Y (toward
// chapel front, away from apse). Top step at chapel-floor level.
// Single wide flight spanning the full chapel interior width.
// MUST stay in sync with SINGLE_FLIGHT_HALF_WIDTH in
// games/selva-oscura/scripts/blender/gen_crypt_foundation.py — both
// must equal chapel interior half-width (BODY_WIDTH/2 - WALL_THICKNESS).
constexpr float kSingleFlightHalfWidth = 2.40f;
constexpr int kUpperFlightStepCount = 9;
constexpr float kUpperFlightTopChapelLocalY = 0.0f;                    // chapel-local Y of top step
constexpr float kUpperFlightDrop = kUpperFlightStepCount * kStairRise; // 1.44m
constexpr float kUpperFlightRun = kUpperFlightStepCount * kStairTread; // 3.15m

// Continuous descent (from bottom of upper flight to Limbo). Must
// match CONTINUOUS_DESCENT_STEP_COUNT in gen_crypt_foundation.py —
// the .glb has this many descent_step_NNNN nodes. Total descent
// footprint length = (upper flight + continuous descent) × tread.
constexpr int kContinuousDescentStepCount = 400;
constexpr float kContinuousDescentDrop = kContinuousDescentStepCount * kStairRise; // 64.00m
constexpr float kContinuousDescentRun = kContinuousDescentStepCount * kStairTread; // 140.00m
// Full descent footprint along chapel-local Y from chapel center (=
// world Z=-210). Both upper flight + continuous descent extend in
// +chapel-local-Y direction (= world -Z) per kDescentForwardSign=+1.
constexpr float kFullDescentRun = kUpperFlightRun + kContinuousDescentRun; // 143.15m
// Sign of the forward direction of descent in chapel-local Y. +1 means
// the corridor extends in +Y (toward the apse = world -Z = toward the
// sun). -1 means -Y (toward chapel front = world +Z = toward spawn).
// Must match gen_crypt_foundation.py's DESCENT_FORWARD_SIGN.
constexpr float kDescentForwardSign = 1.0f;

// Landing where the two flights meet.
constexpr float kLandingWidthX = 3.0f;
constexpr float kLandingDepthY = 2.0f;
constexpr float kLandingClearance = 3.0f; // ceiling height above landing

// Corridor.
constexpr float kCorridorWidth = 2.5f;
constexpr float kCorridorHeight = 3.0f;
constexpr int kCorridorSegmentCount = 4;
constexpr float kCorridorSegmentRun = 12.0f;   // horizontal per ramp
constexpr float kCorridorSegmentDrop = 14.5f;  // vertical per ramp
constexpr float kCorridorLandingLength = 2.0f; // flat landing between ramps

// Limbo platform at the bottom of the descent. The first circle of
// Hell per Inferno cosmology — the widest, populated by virtuous
// pagans. Acheron is the river that runs through Limbo (added as a
// feature later).
constexpr float kLimboPlatformHalfExtent = 12.0f;

// Chapel's ground Y as a fixed design constant. Decoupled from the
// terrain heightmap on purpose: prior versions sampled terrain at an
// "anchor point" near the chapel, but terrain mesh vertex spacing
// (~2.7m) is coarser than chapel-scale features, so any pit/edge
// near the chapel pulled the anchor value around unpredictably and
// the chapel's world Y drifted with it. Pinning the value here means
// the chapel sits at a stable, designer-controlled height regardless
// of any terrain mesh changes.
//
// Chapel ground Y — chosen so the chapel sits visibly above the
// surrounding colle plateau on its foundation skirt. The skirt
// primitive (build_foundation_skirt in gen_crypt_foundation.py)
// extends from chapel-floor-Y down to ~Y=-3, bridging any terrain
// height around the chapel. Terrain stays at its natural
// heightmap-Y and meets the skirt's vertical face — no plateau
// modifier required.
//
// To lower the entire chapel/descent stack: change this constant AND
// scene.json's chapel world_origin.y (matched value). The descent
// stairs are baked at chapel-local-Y inside crypt.glb so they ride
// along automatically via world_origin. Limbo is its own terrain
// region (see assets/world/terrain/config.json: limbo.y_offset)
// and tracks the chapel via the y_offset value.
constexpr float kChapelGroundY = 22.3f;

// World-space translation applied to chapel mesh local coords. Source
// of truth shared by:
//   * render/WorldRenderer.cpp cryptModelMatrix() (sets the GL model
//     matrix when drawing the mesh)
//   * world/PhysicsScene.cpp registerChapel() (bakes the transform
//     into the Jolt static trimesh body so physics sees the chapel at
//     the same world location as the renderer)
// The 0.05m Z-fight offset keeps the chapel's plinth top safely above
// the terrain plateau Y at coplanar boundaries. 0.01m was inside the
// depth-buffer precision flicker range — produced visible z-fight on
// the plinth ring around the chapel perimeter as the camera moved
// (plinth top = 32.26, terrain plateau = 32.25, 1cm gap not enough).
// 5cm puts the gap well outside depth precision flicker without being
// visually noticeable as a step up onto the plinth.
inline glm::vec3 chapelWorldOrigin()
{
    constexpr float kZFightOffset = 0.05f;
    return {kCryptX, kChapelGroundY - kPlinthHeight + kZFightOffset, kCryptZ};
}

// Register the chapel's terrain modifiers (FlushAt / DepressTo)
// with engine::world::TerrainModifiers. Called once at scene init,
// BEFORE the terrain mesh is built — modifiers must exist when
// Terrain.cpp::buildRegionMesh runs.
//
// Two modifiers per chapel:
//
// 1. PLATEAU at chapel footprint — terrain rises to meet the plinth
//    top (y = kChapelGroundY = 32.25). With a 2m soft pad outside
//    the chapel walls, terrain smoothly ramps from natural elevation
//    up to plinth top. Eliminates the cliff at chapel-edge.
//
// 2. PLATEAU strip along the descent slope — terrain meets the
//    descent ceiling at each Y, so terrain naturally caps the
//    tunnel from above (no exposed ceiling top for the player to
//    walk on). This makes the `descent_ceiling` mesh unnecessary
//    for occlusion; it can stay as visible-interior detail.
//
// The descent corridor + limbo are far underground (y down to
// ~-30). Terrain doesn't need to depress that low because the
// chapel mesh (stair shaft enclosure + descent walls + ceiling)
// already seals the tunnel from terrain above.
void registerChapelTerrainModifiers();

// Registers Limbo's terrain features (currently the Acheron trench)
// as runtime TerrainModifiers in the "limbo" region. Must run BEFORE
// world::initTerrain() builds the region meshes.
void registerLimboTerrainModifiers();

// Registers Limbo's point-light set. v1: ~6 dying-inhabitants' lights
// scattered across the FAR shore of Acheron (the side opposite the
// descent stair) — where the lost souls congregate. The near shore
// stays dark, motivating the player's first crafting task (torch).
// See games/selva-oscura/docs/design/limbo.md "Light direction".
void registerLimboLights();

// Single entry point that registers the entire authored world
// (structure footprints, terrain modifiers, lights, anything else
// that lives in the registries before initTerrain runs). Both the
// game's main.cpp AND the build-time dump-world binary call this —
// guaranteeing the same registry state in-game and at bake time.
// Add new world-build helpers here, not at the call sites.
void registerAuthoredWorld();
} // namespace crypt_layout
} // namespace selva::world
