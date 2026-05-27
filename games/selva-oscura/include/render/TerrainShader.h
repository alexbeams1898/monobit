#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
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
void setTerrainShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                      const glm::vec3& shadow_cam_pos, int shadow_texture_unit);
void setTerrainChapelDiscard(const glm::vec2& center, const glm::vec2& half_extents);
void setTerrainApseDiscard(const glm::vec2& center, float radius);
void setTerrainDescentDiscard(const glm::vec2& center, const glm::vec2& half_extents);

// Read back the last-set discard values (for debug overlay display).
glm::vec2 lastTerrainChapelDiscardCenter();
glm::vec2 lastTerrainChapelDiscardHalfExtents();
glm::vec2 lastTerrainApseDiscardCenter();
float lastTerrainApseDiscardRadius();
glm::vec2 lastTerrainDescentDiscardCenter();
glm::vec2 lastTerrainDescentDiscardHalfExtents();

} // namespace selva::render
