#pragma once

#include <glm/mat4x4.hpp>

namespace selva::render
{

// Build the scene shader program (single grayscale-shade pass with
// uModel / uViewProj / uTint uniforms). Returns true on success;
// false if compilation or linking failed (caller treats as fatal).
// Resolves uniform locations after link.
bool initSceneProgram();
void shutdownSceneProgram();

// Bind the program for subsequent draw calls.
void useSceneProgram();

// Per-draw uniform setters. View-projection is set once per frame;
// model + tint are set per object.
void setSceneViewProj(const glm::mat4& view_proj);
void setSceneModel(const glm::mat4& model);
void setSceneTint(float tint);

} // namespace selva::render
