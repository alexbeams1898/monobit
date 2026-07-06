#pragma once

namespace selva::render
{

// Shared display-curve tonemap snippet. Concatenated into any fragment
// shader that produces linear-light radiance and needs to map it into
// the [0,1] display range before output.
//
// SOURCE OF TRUTH for the tonemap step that was previously inlined
// identically in RegionShaders / TerrainShader / TreeShader / SkyPass.
//
// Provides:
//   - `uniform float uExposure;`  -- caller pumps once per frame via
//     setRegionExposure / setTerrainExposure / setTreeExposure / etc.
//   - `vec3 tonemap(vec3 linear)` -- exposure-scaled Reinhard
//     `(L*E) / (L*E + 1)`. Reinhard is mathematically correct for a
//     linear-space input; the caller is responsible for producing
//     linear radiance before invoking this.
//
// After the sRGB-framebuffer rewrite, the GPU re-encodes linear ->
// sRGB on framebuffer write automatically; the tonemap step here
// stays exactly as-is. The bug we're fixing isn't the tonemap;
// it's the missing display-encode AFTER the tonemap.
inline const char* kTonemapGLSL = R"glsl(
uniform float uExposure;

vec3 tonemap(vec3 linearColor)
{
    vec3 c = linearColor * uExposure;
    return c / (c + vec3(1.0));
}
)glsl";

} // namespace selva::render
