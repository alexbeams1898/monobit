#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace selva::render
{

// Build the camera view-projection matrix for the current frame.
// ThirdPerson: over-shoulder, target_lookat_y feeds the smoothed
// look-at height. head_world_mat unused.
// FirstPerson: camera anchored at the head bone position. If
// `use_anim_orientation` is true the camera also takes the head
// bone's full world rotation (cinematic camera-roll for dodge/roll
// one-shots); otherwise the camera uses cameraYaw()/cameraPitch()
// for mouse-driven look. The caller resolves head_world_mat from
// the sampler; pass identity if the bone is unavailable.
// `player_yaw` and `one_shot_name` are diagnostic-only: used by the
// fpv-roll-debug log when enabled.
glm::mat4 buildViewProj(const glm::vec3& player_pos, float target_lookat_y,
                        const glm::vec3& head_world_pos, const glm::mat4& head_world_mat,
                        bool use_anim_orientation, float player_yaw, const char* one_shot_name);
const glm::mat4& lastView();

// Multi-pass environment draw. Caller binds the appropriate shader
// before each:
//   useTerrainShader() + setTerrainAtmosphere(...) -> renderTerrain()
//   useRegionProgram()  + setSceneAtmosphere(...)  -> renderGroundDecals()
//                                                 + renderStaticMeshes()
//   useTreeShader()    + setTreeAtmosphere(...)   -> renderTrees()
// Stencil pre-pass: writes chapel-floor primitives into the stencil
// buffer so the following renderTerrain() pass rejects fragments
// inside the chapel indoor perimeter. Pixel-perfect carve-out using
// the chapel mesh as its own mask source. Call this AFTER clearing
// the stencil buffer and BEFORE renderTerrain(). renderTerrain
// disables stencil test at the end so subsequent passes aren't
// constrained.
void renderChapelFloorMask();
void renderTerrain();
void renderGroundDecals();
void renderStaticMeshes();
void renderTrees();

// Depth-pass variants for shadow map. The caller must have already
// activated the corresponding ShadowPass depth program. Each iterates
// the same geometry as its main counterpart but with depth-only output.
void renderTerrainDepth();
void renderStaticMeshesDepth();
void renderTreesDepth();

} // namespace selva::render
