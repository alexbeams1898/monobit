#pragma once

#include <glm/vec3.hpp>

namespace selva::gameplay
{

struct PlayerState
{
    // XZ-only on the floor plane; Y is unused for gameplay (renderer
    // plants feet via -foot_offset_y).
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
    float yaw = 0.0f;       // facing yaw in radians; 0 = facing -Z
    bool sprinting = false; // true while Space held past sprint commit
};

PlayerState& player();

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
float wrapAngleSigned(float delta);

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z).
float yawFromGroundDir(const glm::vec3& dir);

} // namespace selva::gameplay
