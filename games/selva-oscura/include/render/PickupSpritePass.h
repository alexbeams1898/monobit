#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Pickup sprite pass: renders each live selva::loot::Pickup as a
// world-space billboard (camera-facing quad) with soft radial glow.
// Depth-tested against the 3D scene so terrain, walls, and meshes
// occlude pickups behind them -- replaces the prior ImDrawList HUD-
// overlay path that drew through everything (including the ground
// when the camera was in Limbo below the Wood). Mirrors
// LightSpritePass architecture: same instanced billboard + additive
// blend + depth-test-on / depth-write-off.
//
// Distance-falloff convention: full brightness within near_distance,
// fades to zero at far_distance, invisible beyond. Tunable so the
// pickup reads as "discovery reward when you get close" rather than
// "navigation beacon visible across the map."
//
// Order: AFTER opaque geometry (so depth buffer is populated), BEFORE
// UI. Called alongside renderLightSprites in the render pipeline.

namespace selva::render
{

bool initPickupSpritePass();
void shutdownPickupSpritePass();

// Draw all live loot::Pickups as depth-tested billboards. View/proj
// matrix and camera position required to orient the billboards and
// drive distance falloff.
void renderPickupSprites(const glm::mat4& view_proj, const glm::vec3& cam_pos);

} // namespace selva::render
