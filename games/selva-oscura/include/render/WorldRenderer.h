#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y);
const glm::mat4& lastView();

// Three-pass environment draw. Caller binds the appropriate shader
// before each:
//   useTerrainShader() + setTerrainAtmosphere(...) -> renderTerrain()
//   useSceneProgram()  + setSceneAtmosphere(...)  -> renderGroundDecals()
//   useTreeShader()    + setTreeAtmosphere(...)   -> renderTrees()
void renderTerrain();
void renderGroundDecals();
void renderTrees();

} // namespace selva::render
