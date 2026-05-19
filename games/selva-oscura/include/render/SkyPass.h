#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

bool initSkyPass();
void shutdownSkyPass();

// Render the sky as a fullscreen quad via ray-marched single
// scattering. sun_dir is normalized inside; cam_pos is world-space
// (Y=ground); sun_intensity is the directional irradiance (white-ish
// vector ~tens-of-thousands in atmosphere-model units); exposure
// post-scales before tonemap.
void drawSky(const glm::mat4& inv_view_proj, const glm::vec3& sun_dir,
             const glm::vec3& sun_intensity, const glm::vec3& cam_pos, float exposure);

} // namespace selva::render
