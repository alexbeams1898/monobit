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
} // namespace crypt_layout
} // namespace selva::world
