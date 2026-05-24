#pragma once

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
constexpr float kHalfWidth = 3.0f;   // body_width/2 = 6m/2
constexpr float kHalfLength = 4.0f;  // body_length/2 = 8m/2
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
constexpr float kSingleFlightHalfWidth = 1.50f;  // = LANDING_WIDTH_X / 2 so the descent path width matches the landing's; the back-wall tunnel and chapel-floor hole share this width
constexpr int kUpperFlightStepCount = 9;
constexpr float kUpperFlightTopChapelLocalY = 0.0f; // chapel-local Y of top step
constexpr float kUpperFlightDrop = kUpperFlightStepCount * kStairRise;   // 1.44m
constexpr float kUpperFlightRun = kUpperFlightStepCount * kStairTread;   // 3.15m
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
constexpr int   kCorridorSegmentCount = 4;
constexpr float kCorridorSegmentRun = 12.0f;     // horizontal per ramp
constexpr float kCorridorSegmentDrop = 14.5f;    // vertical per ramp
constexpr float kCorridorLandingLength = 2.0f;   // flat landing between ramps

// Acheron stub at the bottom.
constexpr float kAcheronPlatformHalfExtent = 12.0f;

// Chapel's ground Y as a fixed design constant. Decoupled from the
// terrain heightmap on purpose: prior versions sampled terrain at an
// "anchor point" near the chapel, but terrain mesh vertex spacing
// (~2.7m) is coarser than chapel-scale features, so any pit/edge
// near the chapel pulled the anchor value around unpredictably and
// the chapel's world Y drifted with it. Pinning the value here means
// the chapel sits at a stable, designer-controlled height regardless
// of any terrain mesh changes.
//
// Value chosen so the plinth (zoccolo) BOTTOM sits exactly on the
// surrounding plateau terrain (runtime measurement: groundHeight
// just outside chapel reads ~31.96m; model origin Y = kChapelGroundY
// - kPlinthHeight + kZFightOffset = 31.96m means plinth bottom flush
// with ground and the full 0.30m plinth reveals above as an
// authentic Italian Romanesque zoccolo). If the plateau height
// changes in terrain config, update this constant to match — the
// chapel does NOT chase terrain.
constexpr float kChapelGroundY = 32.25f;
} // namespace crypt_layout
} // namespace selva::world
