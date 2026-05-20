#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

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

} // namespace selva::render
