#include "render/Atmosphere.h"

#include "Tunables.h"

#include <glm/geometric.hpp>

#include <cmath>

namespace selva::render::atmosphere
{

// Atmosphere state lives in selva::tuning::current().lighting; this
// module is now a thin pass-through that normalizes the sun direction
// on read. The F1 Lighting tab edits the tuning fields directly. See
// include/Tunables.h's Lighting struct for per-field doctrine.

glm::vec3 sunDirection()
{
    const auto& raw = selva::tuning::current().lighting.sun_dir;
    const float len2 = raw.x * raw.x + raw.y * raw.y + raw.z * raw.z;
    if (len2 < 1e-12f)
        return glm::vec3(0.0f, 1.0f, 0.0f); // degenerate -- straight up fallback
    return raw * (1.0f / std::sqrt(len2));
}

glm::vec3 sunIntensity()
{
    return selva::tuning::current().lighting.sun_intensity;
}

float exposure()
{
    return selva::tuning::current().lighting.exposure;
}

} // namespace selva::render::atmosphere
