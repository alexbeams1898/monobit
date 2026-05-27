#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace selva::render
{

// Single directional-light shadow map.
//
// The sun is a directional light coming from the colle (Z=-220). We
// render the scene's depth from the sun's POV into a high-resolution
// 2D texture; the main passes sample that texture to decide whether
// each fragment is in shadow.
//
// Light-space camera is orthographic (parallel rays - directional
// light), centered on the player, sized to cover a fixed play radius.
// As the player walks the box follows but the orientation is fixed
// (no rotation) so the shadow seams don't crawl when the camera
// turns. Snap-to-texel for the camera origin would further reduce
// crawl; deferred for now since the play radius is small.
//
// Single-cascade for v1. Cascaded variant is a future polish lift
// when shadow resolution close to the camera becomes the limiting
// factor.

bool initShadowPass();
void shutdownShadowPass();

// Bind the depth-pass shader program for a given geometry family,
// upload uLightViewProj. The caller then issues draw calls against
// the geometry's regular VAOs - the depth program reads aPos from
// the same location all main programs use.
void useTerrainDepthShader();
void useTreeDepthShader();
void useSceneDepthShader();
void useSkeletalDepthShader(); // requires bone palette upload via uBones

// Discard terrain shadow casts inside the chapel exterior footprint.
// Mirror of the main TerrainShader's chapel discard so the depth pass
// doesn't write shadow geometry the main pass omitted.
void setTerrainDepthChapelDiscard(const glm::vec2& center, const glm::vec2& half_extents);
void setTerrainDepthApseDiscard(const glm::vec2& center, float radius);
void setTerrainDepthDescentDiscard(const glm::vec2& center, const glm::vec2& half_extents);

// Per-program uniform setters for the skeletal depth shader. The
// other depth shaders only need uLightViewProj + a model matrix
// where applicable; this is broken out because skeletal needs the
// bone palette every actor.
void setSkeletalDepthModel(const glm::mat4& model);
void setSkeletalDepthBones(const glm::mat4* bone_palette, int count);
void setSceneDepthModel(const glm::mat4& model);
void setTreeDepthModel(const glm::mat4& model);
void setTreeDepthWind(float time, float wind_phase);
void setTreeDepthAlphaCutoff(float cutoff);

// Recompute the light-space view-proj for this frame from the
// player's world position. Call once per frame BEFORE the depth pass.
void updateShadowCamera(const glm::vec3& player_world_pos);

// Bind the shadow FBO + viewport for the depth pass. Geometry depth
// shaders should be active between this call and endDepthPass().
void beginDepthPass();

// Unbind the shadow FBO, restore the previous viewport. Call after
// every depth caster has been drawn.
void endDepthPass(int restore_w, int restore_h);

// Activate the shadow texture on texture unit `unit` so main-pass
// shaders can sample uShadowMap.
void bindShadowTexture(int unit);

// Light-space view-projection matrix, used by both depth-shader
// vertex programs and by main shaders that sample the shadow map.
const glm::mat4& lightViewProj();

// Map resolution (square). 2048 by default.
int shadowMapResolution();

// Diagnostic: depth texture handle. Used only by debug overlays.
std::uint32_t shadowDepthTexture();

} // namespace selva::render
