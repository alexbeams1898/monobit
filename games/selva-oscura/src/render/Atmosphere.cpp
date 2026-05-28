#include "render/Atmosphere.h"

#include <glm/geometric.hpp>

namespace selva::render::atmosphere
{

namespace
{

// Direction TO the sun. The wood's sun is the twilight beacon over
// the colle (-Z). Elevation lifted from ~3.5deg (true horizon, pure
// scattering math) to ~25deg so cast shadows are visually grounded
// rather than horizon-stretched ten-times-the-caster's-height. The
// sky pass still reads as deep dusk because the Rayleigh+Mie
// scattering tints stay tuned for low-elevation jewel-tone. Bump
// back toward horizon if a "true sunset" sky shape becomes the
// priority.
constexpr glm::vec3 kSunDirRaw(0.0f, 0.42f, -0.91f);

// Mystical-register sun radiance. Warm: red highest, green mid, blue
// lowest. The atmosphere scattering converts this into the cool-violet
// sky (Rayleigh on blue) + warm jewel disc (Mie around the sun).
constexpr glm::vec3 kSunIntensity(11.0f, 9.5f, 7.0f);

constexpr float kExposure = 1.0f;

} // namespace

glm::vec3 sunDirection()
{
    return glm::normalize(kSunDirRaw);
}

glm::vec3 sunIntensity()
{
    return kSunIntensity;
}

float exposure()
{
    return kExposure;
}

} // namespace selva::render::atmosphere
