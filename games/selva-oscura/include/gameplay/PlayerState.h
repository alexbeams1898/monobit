#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace selva::gameplay
{

struct PlayerState
{
    // XZ-only on the floor plane; Y is unused for gameplay (renderer
    // plants feet via -foot_offset_y).
    glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
    float yaw = 0.0f; // facing yaw in radians; 0 = facing -Z

    // Ground-plane velocity (m/s). The locomotion system is
    // velocity-driven: WASD applies acceleration toward a target
    // velocity (walk speed or sprint speed); release decelerates
    // toward zero. The animation clip-blend reads `length(velocity_xz)`
    // and picks a weighted mix of idle/walking/running based on
    // speed thresholds — no discrete state machine, no transition
    // clips, no debounce mismatches.
    glm::vec2 velocity_xz = glm::vec2(0.0f);

    // Latched intent flag: true while Space is held past the sprint
    // commit. Drives target_speed = run_speed (vs walk_speed).
    // No longer drives the SM's clip pick directly — that's now
    // velocity-magnitude-driven. Still used by combat (Sprint+LMB
    // fires running attack).
    bool sprinting = false;
};

PlayerState& player();

// Wrap a yaw delta into [-pi, +pi] so rotation always takes the short path.
float wrapAngleSigned(float delta);

// Map a unit ground-plane vector (X, _, Z) to a yaw matching our convention:
// yaw=0 faces -Z, positive yaw rotates CCW looking down. atan2(-x, -z).
float yawFromGroundDir(const glm::vec3& dir);

} // namespace selva::gameplay
