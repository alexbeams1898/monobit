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

// Pass scope. drawSkeletalMesh expects the program bound, culling
// disabled (Mixamo/Quaternius rigs have inconsistent winding), and
// alpha-blend state managed per draw. Call beginSkeletalPass() ONCE
// before drawing any skeletal mesh in a frame; call endSkeletalPass()
// after the last one to restore default GL state for the next pass
// (terrain / static meshes assume cull-back-enabled).
void beginSkeletalPass();
void endSkeletalPass();

// Draw a skinned mesh. MUST be called between beginSkeletalPass /
// endSkeletalPass.
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
// alpha        : per-draw transparency (1.0 = fully opaque, 0.0 =
//                invisible + drawcall skipped). When alpha < 1.0, blending
//                is enabled for this draw + depth-write disabled so
//                transparent bodies don't occlude each other. Caller
//                must draw opaque actors BEFORE transparent ones, and
//                must sort transparent draws back-to-front to avoid
//                alpha-order artifacts when multiple fading corpses
//                overlap.
void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint,
                      float alpha = 1.0f);

// Per-frame sun direction (normalized) for skeletal lambert shading.
// Called once before drawing skeletal meshes.
void setSkeletalSun(const glm::vec3& sun_dir);

// Per-frame shadow sampling uniforms. Set once before the skeletal
// draw block; subsequent drawSkeletalMesh calls inherit them.
void setSkeletalShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                       const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

} // namespace selva::anim
