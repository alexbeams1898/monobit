#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Pickup sprite pass: renders each live selva::loot::Pickup as a
// vertical PILLAR OF LIGHT growing from the ground -- the Elden-Ring
// drop convention. World-up quad billboarded around its Y axis so it
// always presents its broad face to the camera but never tilts off
// vertical. Fragment shader gives a hot core + soft horizontal
// falloff and a candleflame-style vertical taper-to-point.
// Depth-tested against the 3D scene so terrain, walls, and meshes
// occlude pillars behind them. Instanced quad + additive blend +
// depth-test-on / depth-write-off.
//
// The pillar IS the discovery cue: tuned to read across a Selva
// clearing, not just at close range. Layered on top of any
// world_mesh, so a Wood material pickup shows both the mesh (when
// you walk up) and the pillar (from across the area).
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
