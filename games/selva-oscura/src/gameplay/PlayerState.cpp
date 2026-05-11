#include "gameplay/PlayerState.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace selva::gameplay
{

// `player()` and the actor pool live in Actor.cpp. PlayerState
// is now an Actor alias — see PlayerState.h. This file keeps the
// player-specific helpers (yaw math) and the initPlayer entry.

void initPlayer()
{
    // initActorPool resets the pool and creates the Actor at index 0
    // as the player (controller=Input, faction=Player). The player
    // accessor `player()` (defined in Actor.cpp) returns this entry.
    initActorPool();
}

float wrapAngleSigned(float delta)
{
    while (delta > glm::pi<float>())
        delta -= glm::two_pi<float>();
    while (delta < -glm::pi<float>())
        delta += glm::two_pi<float>();
    return delta;
}

float yawFromGroundDir(const glm::vec3& dir)
{
    return std::atan2(-dir.x, -dir.z);
}

} // namespace selva::gameplay
