#pragma once

namespace selva::render
{

// Shared world-lighting constants. Concatenated into any fragment
// shader that needs the standard outdoor ambient + sun palette.
//
// SOURCE OF TRUTH for three colors that were previously triplicated
// across RegionShaders / TerrainShader / TreeShader:
//
//   kSkyAmbient    -- overcast bluish overhead, drives the up-facing
//                     half of the hemispheric ambient mix.
//   kGroundAmbient -- dim warm dirt bounce, drives the down-facing
//                     half of the hemispheric ambient mix.
//   kSunTint       -- warm directional sun color, multiplied with
//                     half-Lambert + shadow.
//
// AS OF 2026-06-30 these are RUNTIME UNIFORMS, not GLSL consts.
// Values are sourced from selva::tuning::current().lighting and
// pushed via setAmbientConstants() each frame. Lets the F1 panel
// drive them live without rebuild.
//
// Each consuming shader (Region, Terrain, Tree, Skeletal) must:
//   1. Concatenate kAmbientConstantsGLSL into its fragment source
//      (which declares the uniforms below).
//   2. Cache its three uniform locations at init time.
//   3. Call setAmbientConstants(prog) before draw, OR receive them
//      via the shared setter the per-frame plumbing wires.
//
// The TerrainShader path's uSkyAmbient / uGroundAmbient uniforms
// (per-region authored ambients from JSON) take precedence WHERE
// SET; the constants here are the default-baseline. Region/tree/
// skeletal use these directly.
//
// All values LINEAR-SPACE. Defaults track Tunables::Lighting.
inline const char* kAmbientConstantsGLSL = R"glsl(
uniform vec3 uKSkyAmbient;
uniform vec3 uKGroundAmbient;
uniform vec3 uKSunTint;
// Backwards-compat aliases so existing shader bodies using the old
// const names (kSkyAmbient/kGroundAmbient/kSunTint) keep compiling
// without an edit at every reference site. Trivially elided by the
// driver.
#define kSkyAmbient    uKSkyAmbient
#define kGroundAmbient uKGroundAmbient
#define kSunTint       uKSunTint
)glsl";

} // namespace selva::render
