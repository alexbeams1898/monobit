#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

// Build view-projection matrix from current camera state + player
// position + smoothed lookAt-Y. Side-effect: updates the smoothed
// lookAt-Y per frame using SDL_GetTicks64 dt.
glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y);

// Draw the world environment: floor, grid + axes, scene cube. Caller
// must have built+set view-projection on the scene shader before
// calling.
void renderEnvironment();

} // namespace selva::render
