#pragma once

#include <glm/vec3.hpp>

namespace selva::render
{

// Frame-scoped resolved lighting environment. The per-frame plumbing
// picks the active terrain region (by camera XZ) and pushes its
// authored ambient / sun_multiplier / sun_tint values HERE. All
// shaders' use...Shader() functions then read from here instead of
// pulling directly from Tunables — so characters, static meshes, and
// foliage inherit the CORRECT ambient for wherever the camera is
// standing (dark cavern in Limbo vs. sunlit Wood on the surface).
//
// This was previously bugged: SkeletalRenderer / RegionShaders /
// TreeShader all read Tunables.lighting directly, which is the
// GLOBAL / surface-tuned value regardless of where the camera is.
// Terrain shader had per-region overrides via setTerrainLightingEnv;
// the other three did not, so characters + walls + trees kept
// sunlit-Wood ambient even when the camera was underground.
struct ActiveLightingEnv
{
    glm::vec3 sky_ambient{0.0f};
    glm::vec3 ground_ambient{0.0f};
    glm::vec3 sun_tint{0.0f};
    float sun_multiplier = 1.0f;
};

// Set the frame's active environment. Called once per frame from
// PerFrameTick BEFORE the render passes run. Cheap; just stores the
// values in a module-static.
void setActiveLightingEnv(const ActiveLightingEnv& env);

// Read the frame's active environment. Called by every use...Shader()
// function to push the SAME values through as ambient uniforms —
// so all lit surfaces (terrain via its per-region override, skeletal,
// static mesh, foliage) light with a coherent palette.
const ActiveLightingEnv& activeLightingEnv();

} // namespace selva::render
