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
// disabled (third-party skinned rigs have inconsistent triangle
// winding), and alpha-blend state managed per draw. Call
// beginSkeletalPass() ONCE before drawing any skeletal mesh in a
// frame; call endSkeletalPass() after the last one to restore default
// GL state for the next pass (terrain / static meshes assume
// cull-back-enabled).
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
// morph_weights: per-actor weights in the same order as mesh.morph_names
// (indexable by morph_idx). Empty disables the morph pipeline (vertex
// shader's uMorphCount=0 = early-exit). When non-empty, must match
// mesh.morph_target_count entries -- excess is dropped, deficit is
// treated as trailing zeros. Used by the player creator + future
// per-actor face customization; enemies pass {} (default) since they
// don't have morphs today.
void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint,
                      float alpha = 1.0f, const std::vector<float>& morph_weights = {});

// Per-frame sun direction (normalized) for skeletal lambert shading.
// Called once before drawing skeletal meshes.
void setSkeletalSun(const glm::vec3& sun_dir);

// Per-frame exposure multiplier driving the Reinhard tonemap.
// Default 1.0 if never set. Matches terrain/region/tree/sky semantics.
void setSkeletalExposure(float exposure);

// Per-frame shadow sampling uniforms. Set once before the skeletal
// draw block; subsequent drawSkeletalMesh calls inherit them.
void setSkeletalShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                       const glm::vec3& shadow_cam_pos, int shadow_texture_unit);

// Override the hemispheric ambient + sun-tint values for the next
// beginSkeletalPass(). Defaults to Tunables.lighting (world look).
// The character preview overrides these to use a softer portrait
// fill so downward-facing surfaces (legs, palms) don't go pure
// black against an empty background. Call BEFORE beginSkeletalPass.
//
// Call clearSkeletalAmbientOverride() after the pass to return to
// the world defaults; otherwise gameplay inherits the portrait fill.
void setSkeletalAmbientOverride(const glm::vec3& sky_ambient, const glm::vec3& ground_ambient,
                                const glm::vec3& sun_tint);
void clearSkeletalAmbientOverride();

// How the tint uniform composes with the sampled diffuse for the NEXT
// drawSkeletalMesh call. Auto-resets to Multiply after each draw so
// state is opt-in and doesn't leak to subsequent actors. Default is
// Multiply (body + eyes: subtle re-tint). Colorize desaturates the
// diffuse to luminance THEN multiplies by tint, so the tint drives
// the final HUE while strand detail survives. Used by HairRenderer
// so a red tint on brown hair actually looks red, not warm-brown.
enum class TintMode : int
{
    Multiply = 0,
    Colorize = 1,
};
void setSkeletalTintMode(TintMode mode);

// Per-actor eye colour. When set, any primitive in the next
// drawSkeletalMesh call whose material.role == Eyes gets tinted in
// Colorize mode with this colour; other primitives keep the actor's
// body tint. Auto-resets after the draw so eye colour doesn't leak
// between actors -- each actor's drawer must set its own if needed.
// Skip setting to leave eye colour at the baked default (no override).
void setSkeletalEyeTint(const glm::vec3& linear_rgb);

} // namespace selva::anim
