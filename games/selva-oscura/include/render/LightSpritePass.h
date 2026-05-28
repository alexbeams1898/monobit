#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace engine::world
{
struct LightSource;
}

namespace selva::render
{

// Light sprite pass: renders each registered point light as a small
// world-space billboard (camera-facing quad) with a soft radial glow.
// Makes lights visible as bright points from any distance — the
// underlying point-light contribution only illuminates ground within
// the light's radius, so without sprites the lights are invisible
// from across a large region (e.g. Limbo's 600m disc).
//
// Order: after opaque geometry, before UI. Additive blend, depth
// test on, depth write off.
//
// Inspired by FromSoft bonfire-flame rendering: the flame itself is
// authored billboards / particles, separate from the bonfire's
// luminous contribution to the ground.
bool initLightSpritePass();
void shutdownLightSpritePass();

// Draw all registered lights as sprites. `region_name` filters lights
// to those tagged with the matching region (or nullptr for all). The
// view/projection matrix + camera position are needed to orient the
// billboards toward the camera.
void renderLightSprites(const glm::mat4& view_proj, const glm::vec3& cam_pos,
                        const char* region_name);

} // namespace selva::render
