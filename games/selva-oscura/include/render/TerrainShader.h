#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <vector>

namespace engine::world
{
struct LightSource;
}

namespace selva::render
{

bool initTerrainShader();
void shutdownTerrainShader();
void useTerrainShader();

void setTerrainViewProj(const glm::mat4& view_proj);
void setTerrainAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                          const glm::vec3& cam_pos, float exposure);
void setTerrainBaseColor(const glm::vec3& color);
void setTerrainTones(const glm::vec3& dark_loam, const glm::vec3& dry_dirt);
// Set per-region lighting environment: sun_multiplier 1.0 = full sun
// (outdoor regions), 0.0 = no direct sun (underground caverns). Sky
// and ground ambients drive the hemispheric ambient blend.
void setTerrainLightingEnv(float sun_multiplier, const glm::vec3& sky_ambient,
                           const glm::vec3& ground_ambient);
void setTerrainShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                      const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

// Upload the active point-light set. Iteration order is preserved.
// Lights beyond the shader's max are dropped (logged once). The
// shader sums contributions per fragment; empty list disables the
// point-light pass.
void setTerrainPointLights(const std::vector<engine::world::LightSource>& lights);

// Upload the active discard-rect set. Each rect is packed as
// (center.xy, half_extents.xy). Rects beyond the shader's max are
// dropped (logged once). WorldRenderer's per-region draw loop
// populates this from registered cuts_floor StructureFootprints.
void setTerrainDiscardRects(const std::vector<glm::vec4>& rects);

} // namespace selva::render
