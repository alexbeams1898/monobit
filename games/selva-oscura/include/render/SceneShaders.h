#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

bool initSceneProgram();
void shutdownSceneProgram();
void useSceneProgram();

void setSceneView(const glm::mat4& view);
void setSceneViewProj(const glm::mat4& view_proj);
void setSceneModel(const glm::mat4& model);
void setSceneTint(float tint);

// Per-primitive RGB color factor (1,1,1 = no tint). Used by static
// meshes to carry their glTF baseColorFactor — preserves hue, not
// just luminance, so different materials read distinctly.
void setSceneBaseColor(const glm::vec3& rgb);

// 1.0 when the camera is inside enclosed architecture, 0.0 outside.
// Suppresses the atmospheric in-scatter term, which would otherwise
// paint sky light onto interior surfaces (the scatter math doesn't
// know about wall occlusion).
void setSceneIndoorMode(bool indoors);

// sun_dir is normalized inside; pass any non-zero direction.
void setSceneAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                        const glm::vec3& cam_pos, float exposure);

void setSceneShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                    const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

} // namespace selva::render
