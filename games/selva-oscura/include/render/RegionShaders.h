#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <vector>

namespace engine::world
{
struct LightSource;
}

namespace selva::render
{

bool initRegionProgram();
void shutdownRegionProgram();
void useRegionProgram();

void setSceneView(const glm::mat4& view);
void setSceneViewProj(const glm::mat4& view_proj);
void setSceneModel(const glm::mat4& model);
void setSceneTint(float tint);

// Per-primitive RGB color factor (1,1,1 = no tint). Used by static
// meshes to carry their glTF baseColorFactor — preserves hue, not
// just luminance, so different materials read distinctly.
void setSceneBaseColor(const glm::vec3& rgb);

// Debug: when true, fragment shader skips all lighting and outputs
// flat uBaseColor per primitive. Use to bisect flicker bugs —
// flicker on = shader math (likely dFdx/dFdy normal flip);
// flicker remains = geometry / depth precision / rasterization.
void setSceneFlatShading(bool on);

// sun_dir is normalized inside; pass any non-zero direction.
void setSceneAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                        const glm::vec3& cam_pos, float exposure);

void setSceneShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                    const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

// Upload the active point-light set for subsequent draws. Same packing
// + same cap (MAX_LIGHTS=64) as TerrainShader so any geometry the
// scene program draws picks up the same in-region torches. Empty list
// disables the point-light contribution.
void setScenePointLights(const std::vector<engine::world::LightSource>& lights);

} // namespace selva::render
