#include "gameplay/PlayerState.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace selva::gameplay
{

namespace
{
PlayerState sPlayer;
} // namespace

PlayerState& player()
{
    return sPlayer;
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
