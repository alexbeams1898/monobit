#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace selva::render
{

bool initTreeShader();
void shutdownTreeShader();
void useTreeShader();

void setTreeView(const glm::mat4& view);
void setTreeViewProj(const glm::mat4& view_proj);
void setTreeModel(const glm::mat4& model);

// Per-instance wind phase (0..2pi). Lets every tree sway out of phase
// so the forest doesn't move in unison.
void setTreeWindPhase(float phase);

// Atmosphere: same model as the scene shader so trees light coherently
// with ground/sky.
void setTreeAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                       const glm::vec3& cam_pos, float exposure);

// Bind the base-color texture to texture unit 0.
void setTreeBaseColor(std::uint32_t tex);

// Per-draw alpha-test threshold. Trunks: 0 (no test). Branches: ~0.5.
void setTreeAlphaCutoff(float cutoff);

// Drive the time-uniform for wind. Pass selva::wallClock() each frame.
void setTreeTime(float t);

// Shadow sampling uniforms - light-space view-proj, sun direction for
// bias slope-scaling, and the texture unit holding the depth map.
void setTreeShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                   const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

} // namespace selva::render
