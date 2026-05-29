#pragma once

#include <glm/mat4x4.hpp>

namespace selva::render
{

// Geometry init/shutdown. Call after the GL context exists; teardown
// before the context dies.
void initRegionGeometry();
void shutdownRegionGeometry();

void drawGround(const glm::mat4& model, float tint);
void drawCube(const glm::mat4& model, float tint);
void drawDisc(const glm::mat4& model, float tint);

} // namespace selva::render
