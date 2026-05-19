#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

// Caches the view matrix as a side-effect (see lastView()).
glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y);
const glm::mat4& lastView();

// Draw the world environment: floor, grid + axes, scene cube. Caller
// must have built+set view-projection on the scene shader before
// calling.
void renderEnvironment();

} // namespace selva::render
