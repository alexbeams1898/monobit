#pragma once

#include <glm/mat4x4.hpp>

namespace selva::render
{

// Geometry init/shutdown. Call after the GL context exists; teardown
// before the context dies.
void initSceneGeometry();
void shutdownSceneGeometry();

// Per-frame draw helpers. Each updates uModel + uTint via SceneShaders
// before issuing the draw call. Caller should have already bound the
// scene program and set uViewProj for the frame.
void drawFloor(const glm::mat4& model, float tint);
void drawCube(const glm::mat4& model, float tint);
void drawDisc(const glm::mat4& model, float tint);
void drawGrid(const glm::mat4& model, float tint);
void drawAxes(const glm::mat4& model, float tint);

} // namespace selva::render
