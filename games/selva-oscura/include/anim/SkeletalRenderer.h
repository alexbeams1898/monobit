#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

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
// tint         : grayscale brightness multiplier (matches the existing
//                scene shader's uTint convention, for visual consistency).
//
// Caller is responsible for binding the GL state appropriately for the
// frame (depth test enabled, etc. — the engine already does this).
void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, float tint);

} // namespace selva::anim
