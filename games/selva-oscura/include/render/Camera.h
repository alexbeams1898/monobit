#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class Engine;

namespace selva::render
{

// Camera state (yaw, pitch) and window dimensions. The camera follows
// the player from behind at a tunable follow-distance / follow-height;
// gameplay code reads cameraYaw() to derive the player's forward axis
// for WASD movement.
float cameraYaw();
float cameraPitch();
void setCameraYaw(float yaw);
void setCameraPitch(float pitch);

int windowWidth();
int windowHeight();

// Engine resize callback. Register via engine.setOnResize().
void onWindowResize(::Engine& engine, int new_w, int new_h);

// Cache window dimensions at startup (engine doesn't fire resize on
// initial create).
void setInitialWindowSize(int w, int h);

} // namespace selva::render
