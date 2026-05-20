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

// sun_dir is normalized inside; pass any non-zero direction.
void setSceneAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                        const glm::vec3& cam_pos, float exposure);

} // namespace selva::render
