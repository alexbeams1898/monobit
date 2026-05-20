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

// Project a world-space point to viewport-space pixel coordinates
// using the supplied view-projection matrix. Returns true and writes
// `out_screen` (x, y in pixels) when the point is in front of the
// camera; returns false otherwise (use to skip drawing behind-camera
// elements). Y is in ImGui's top-down convention (0 at top).
bool worldToScreen(const glm::mat4& view_proj, const glm::vec3& world_pos, glm::vec2& out_screen);

// Cache + read the most recently built view-projection matrix.
// The render pass calls setLastViewProj(viewProj); the ImGui pass
// reads it via lastViewProj() to project world positions for
// in-world UI (enemy HP bars, floating damage numbers).
void setLastViewProj(const glm::mat4& m);
const glm::mat4& lastViewProj();

} // namespace selva::render
