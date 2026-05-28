#pragma once

#include <glm/glm.hpp>

#include <vector>

#include <glad/glad.h>

namespace selva::anim
{

struct SkeletalMesh; // forward — full def in anim/SkeletalMesh.h

// Builds the GPU shader program for skinned-mesh rendering. Call once
// after the GL context exists; logs and returns false on compile/link
// failure. Idempotent — safe to call again, second call is a no-op.
bool initSkeletalRenderer();

// Frees GPU resources. Call before the GL context is destroyed (i.e.
// before Engine::shutdown).
void shutdownSkeletalRenderer();

// Draw a skinned mesh.
//
// model        : the entity's world-space transform (translation + yaw etc.)
// view_proj    : the camera's combined view + projection matrix
// bone_palette : per-bone *model-space* transforms, in ozz joint order.
//                Length must match the skeleton's bone count. Each entry
//                is the cumulative transform for that bone (root->bone)
//                in the current pose.
// tint         : RGB color multiplier applied after lambert lighting.
//                (1,1,1) = neutral white, (0.3,0.3,0.3) = dim gray,
//                (0.35,0.10,0.13) = dark bordeaux, etc.
//
// Caller is responsible for binding the GL state appropriately for the
// frame (depth test enabled, etc. — the engine already does this).
void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint);

// Per-frame sun direction (normalized) for skeletal lambert shading.
// Called once before drawing skeletal meshes.
void setSkeletalSun(const glm::vec3& sun_dir);

// Per-frame shadow sampling uniforms. Set once before the skeletal
// draw block; subsequent drawSkeletalMesh calls inherit them.
void setSkeletalShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                       const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

} // namespace selva::anim
