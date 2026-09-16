#pragma once

namespace selva::render
{

// GLSL snippet defining sampleSunShadow(vec3 worldPos) for any main
// fragment shader. The caller must concatenate this into its FS
// source and call it to attenuate the sunlit term.
//
// Required uniforms (must be declared by the host FS):
//   uniform sampler2DShadow uShadowMap;
//   uniform mat4 uLightViewProj;
//
// Returns 1.0 (fully lit) when no occlusion, 0.0 (fully shadowed)
// when fully occluded. Hardware PCF (4-tap bilinear) is enabled via
// sampler2DShadow + GL_LINEAR filter + GL_COMPARE_REF_TO_TEXTURE
// configured in ShadowPass.cpp.
//
// Bias: a small constant + slope-scaled term moves the comparison
// depth slightly toward the light, suppressing self-shadow acne on
// surfaces nearly parallel to the sun direction (terrain slopes,
// vertical tree trunks).
inline const char* kShadowGLSL = R"glsl(
uniform sampler2DShadow uShadowMap;
uniform mat4 uLightViewProj;
uniform vec3 uShadowSunDir;

// 9-tap PCF kernel for soft shadow edges. Step is 1/resolution in
// light-space NDC, independent of world box size.
const float kPcfTexelStep = 1.0 / 4096.0;

// Normal-aligned receiver-side push-off. Displaces the shadow query
// point along the surface normal before projection into light space.
// Structurally-correct anti-self-shadow technique - the receiver no
// longer depth-compares against itself.
// At 4096 res / 80m half-extent the texel size is 3.9cm; 6cm gives
// ~1.5 texels of separation.
const float kNormalOffset = 0.06; // world meters

// uShadowCameraPos retained for future use (distance-based filter
// sizing, e.g. larger PCF kernel at distance). Currently unused —
// the previous distance-based shadow fade was removed because it
// treated "distant from player" as a proxy for "doesn't need shadow,"
// which broke for descent-corridor geometry 100m past the player
// that IS sun-occluded. Real-lighting doctrine: shadow coverage is
// determined by sun line-of-sight to the fragment, not heuristics.
uniform vec3 uShadowCameraPos;

float sampleSunShadow(vec3 worldPos, vec3 worldNormal)
{
    vec3 N = normalize(worldNormal);
    vec3 shadowSamplePos = worldPos + N * kNormalOffset;

    vec4 lp = uLightViewProj * vec4(shadowSamplePos, 1.0);
    vec3 ndc = lp.xyz / lp.w;
    vec3 sc = ndc * 0.5 + 0.5;
    // Fragment outside the shadow map's coverage: we don't have
    // ground-truth occlusion data for it. Returning 1.0 (fully lit)
    // is the wrong answer for indoor/underground fragments past the
    // 80m ortho extent (those are sun-occluded), but the correct
    // answer for distant outdoor terrain. The general fix is wider
    // coverage (cascaded shadow maps) — until that lands, this is
    // the bounded-coverage limit. Document so the next person knows
    // it's a coverage gap, not a doctrine violation.
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0)
        return 1.0;

    float cosTheta = max(dot(N, normalize(uShadowSunDir)), 0.0);
    float bias = 0.0006 * (1.0 - cosTheta);
    sc.z -= bias;

    float accum = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            vec2 off = vec2(float(dx), float(dy)) * kPcfTexelStep;
            accum += texture(uShadowMap, vec3(sc.xy + off, sc.z));
        }
    }
    return accum * (1.0 / 9.0);
}
)glsl";

} // namespace selva::render
