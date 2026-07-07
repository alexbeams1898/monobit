#pragma once

#include "world/Lights.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

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

// Per-frame foliage tint multiplier. Applied as base.rgb *= tint
// in the fragment shader, AFTER texture sample and BEFORE lighting.
// Identity = vec3(1.0); dead-wood = warm grey-brown values < 1.0.
// Per wood.md: the wood begins dead and heals as keepers fall;
// today this is a global value; future restoration plumbs it per-
// region or per-keeper-progress.
void setTreeFoliageTint(const glm::vec3& tint);

// Drive the time-uniform for wind. Pass selva::wallClock() each frame.
void setTreeTime(float t);

// Shadow sampling uniforms - light-space view-proj, sun direction for
// bias slope-scaling, and the texture unit holding the depth map.
void setTreeShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                   const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

// Upload point-light array. Same schema as setTerrainPointLights /
// setScenePointLights / setSkeletalPointLights. Foliage picks up
// torches and campfires against a canopy up-vector normal (leaves
// are treated as diffuse sky-facing volumes).
void setTreePointLights(const std::vector<engine::world::LightSource>& lights);

} // namespace selva::render
