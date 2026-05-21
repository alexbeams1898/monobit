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

// Distance-based shadow fade. Shadow contribution lerps to fully-lit
// over [kFadeStart, kFadeEnd] meters from the player. This absorbs
// the visible artifact of "box edge passes over distant tree -
// suddenly that tree starts casting shadow" - the affected region
// is already in the fade band so the shadow contribution there is
// near-zero and the pop-in is imperceptible.
//
// The fade has to be coordinated with kOrthoHalfXY in ShadowPass.cpp:
// kFadeEnd should be < kOrthoHalfXY so the fade completes inside the
// box. At kOrthoHalfXY=80m, kFadeEnd=70m gives a 10m buffer.
const float kShadowFadeStart = 50.0;
const float kShadowFadeEnd = 70.0;
uniform vec3 uShadowCameraPos;

float sampleSunShadow(vec3 worldPos, vec3 worldNormal)
{
    // Distance-based fade. Skip the actual shadow sample entirely if
    // we're outside the fade-end distance - cheaper AND avoids any
    // edge artifacts on geometry the shadow box happens to clip.
    float distFromCam = length(worldPos - uShadowCameraPos);
    if (distFromCam >= kShadowFadeEnd)
        return 1.0;

    vec3 N = normalize(worldNormal);
    vec3 shadowSamplePos = worldPos + N * kNormalOffset;

    vec4 lp = uLightViewProj * vec4(shadowSamplePos, 1.0);
    vec3 ndc = lp.xyz / lp.w;
    vec3 sc = ndc * 0.5 + 0.5;
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
    float shadow = accum * (1.0 / 9.0);

    // Lerp toward 1.0 (fully lit) across the fade band so the shadow
    // smoothly disappears at distance rather than popping at the
    // hard boundary.
    float fade = smoothstep(kShadowFadeStart, kShadowFadeEnd, distFromCam);
    return mix(shadow, 1.0, fade);
}
)glsl";

} // namespace selva::render
