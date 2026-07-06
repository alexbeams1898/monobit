#include "anim/SkeletalRenderer.h"

#include "Tunables.h"
#include "anim/SkeletalMesh.h"
#include "debug/Flags.h"
#include "gl/ShaderUtils.h"
#include "render/AmbientConstants.h"
#include "render/ShadowShader.h"
#include "render/TonemapShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>

namespace selva::anim
{

namespace
{

// ---------------------------------------------------------------------------
// Shader source
//
// Vertex shader: the conceptual heart of skeletal animation.
//   For each vertex, look up the four bones it's influenced by in the bone
//   palette, build a weighted skin matrix, and transform the rest-pose
//   position into the current pose.
//
//   skinMatrix = w0 * bones[i0] + w1 * bones[i1] + w2 * bones[i2] + w3 * bones[i3]
//   worldPos   = uModel * skinMatrix * vec4(aPos, 1.0)
//
//   The rest-pose vertex positions in the VBO are in *local mesh space*.
//   The bone palette is in *model space* (root-to-bone cumulative). The
//   model matrix puts model space into world space. Multiply in that order.
//
//   Normals get the same skin matrix, but only its rotation/scale part —
//   we strip translation by promoting to a vec4 with w=0.0.
// ---------------------------------------------------------------------------

// Bone palette lives in an SSBO -- no compile-time cap. Rigs of any
// size upload as one buffer-orphan per draw. Binding=0 matches the
// glBindBufferBase call in drawSkeletalMesh.
constexpr GLuint kBonePaletteBinding = 0;
// Morph-target deltas SSBO (per-mesh, static). Indexed
// [morph_idx * vertex_count + gl_VertexID] -> vec4(delta_xyz, 0).
// Mesh.morph_delta_ssbo holds it; binding=1 matches the bind below.
// Per glTF spec, morph deltas apply BEFORE skinning.
constexpr GLuint kMorphDeltasBinding = 1;
// Upper bound on per-actor morph weights. The vertex shader sums a
// fixed-size float[] (uMorphWeights) and gates the loop with
// uMorphCount, so meshes with fewer morphs just don't visit the
// trailing slots. 512 covers the full tiered slider catalog (394
// morphs today: primary + secondary face + body axes) with generous
// headroom. Bumping this is shader recompile + cpu-side array resize,
// both cheap: 512 floats = 2KB uniform buffer, well under any
// GL_MAX_VERTEX_UNIFORM_COMPONENTS limit. The shader-side
// uMorphWeights[N] literal below MUST match this value -- they're
// separate sources because GLSL doesn't take preprocessor defines
// from outside. Overflow symptom: morph data reaches GPU but the
// shader silently ignores indices >= this cap, so mesh doesn't
// deform for morphs at high indices. Alphabetical name order means
// bilateral pairs (l-* and r-*) get truncated on the r-* side
// first -- one-sided deformation is the tell.
constexpr int kMaxMorphWeights = 512;

const char* kVertexShader = R"glsl(
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uViewProj;

layout(std430, binding = 0) readonly buffer BonePalette {
    mat4 uBones[];
};
// Morph-target deltas. Indexed [morph_idx * uVertexCount + gl_VertexID].
// vec4 stride (with .w unused) keeps std430 layout simple. Buffer is
// per-mesh, static, uploaded once at mesh-load time.
layout(std430, binding = 1) readonly buffer MorphDeltas {
    vec4 uMorphDelta[];
};
uniform int uMorphCount;        // active morph count for THIS draw (0 disables)
uniform int uVertexCount;       // stride into uMorphDelta -- needed because
                                // gl_VertexID alone doesn't say how big each
                                // per-target slice is
uniform float uMorphWeights[512];

out vec3 vNormalWorld;
out vec3 vWorldPos;
out vec2 vUV;

void main()
{
    // Morph deltas applied BEFORE skinning per glTF spec
    // (mesh-local-space displacements added to the base position;
    // skinning then transforms the morphed vertex). Identity case
    // (uMorphCount=0) skips the loop entirely -- non-morph meshes
    // pay zero cost.
    vec3 morphedPos = aPos;
    for (int i = 0; i < uMorphCount; ++i)
    {
        morphedPos += uMorphWeights[i] *
                      uMorphDelta[i * uVertexCount + gl_VertexID].xyz;
    }

    mat4 skinMatrix =
        aBoneWeights.x * uBones[aBoneIndices.x] +
        aBoneWeights.y * uBones[aBoneIndices.y] +
        aBoneWeights.z * uBones[aBoneIndices.z] +
        aBoneWeights.w * uBones[aBoneIndices.w];

    vec4 localPos = skinMatrix * vec4(morphedPos, 1.0);
    vec4 worldPos4 = uModel * localPos;
    gl_Position = uViewProj * worldPos4;
    vWorldPos = worldPos4.xyz;

    vec4 localNormal = skinMatrix * vec4(aNormal, 0.0);
    vNormalWorld = normalize(mat3(uModel) * vec3(localNormal));

    vUV = aUV;
}
)glsl";

// Fragment shader: half-Lambert lit by the centralized sun, attenuated
// by the shadow map. Reads vWorldPos from the VS so the shadow sample
// can compare against the depth FBO. The base color is texture *
// factor * uTint -- texture sampling is gated by uHasBaseColorTex so
// material-less meshes (today's default) pay no sample cost.
const char* kFragmentShaderCore = R"glsl(
#version 430 core
in vec3 vNormalWorld;
in vec3 vWorldPos;
in vec2 vUV;

uniform vec3 uTint;
uniform vec3 uSunDir;
uniform float uAlpha;
// Tint application mode. 0 = multiply (default, body + eyes): tint
// scales the sampled diffuse per channel; a red tint on brown hair
// gives a slightly-warmer brown, not red. 1 = colorize (hair): the
// sampled diffuse is desaturated to its luminance and then multiplied
// by the tint, so the tint drives the final HUE while the diffuse's
// strand DETAIL (light/dark variation) is preserved. Souls-convention
// hair customization -- lets the player pick any colour and land on
// that colour regardless of the baked hair's original tone.
uniform int uTintMode;

// Per-material base color. uBaseColorTex is bound to texture unit 0;
// uHasBaseColorTex flips between "use the texture" and "use the
// factor only." uBaseColorFactor multiplies the sample (or stands
// alone when no texture is bound).
uniform sampler2D uBaseColorTex;
uniform bool      uHasBaseColorTex;
uniform vec4      uBaseColorFactor;

// Alpha-test cutoff for Mask alpha-mode materials. 0.0 disables
// (Opaque + Blend materials skip the discard branch entirely).
uniform float     uAlphaCutoff;

// Session-only diagnostic. When non-zero, the shader re-colors
// near-red pixels using (vUV.x, vUV.y, 0) so the UV that produced
// the suspect pixel reads off the screen as a green/red gradient.
// Tied to selva::debug::flags().skeletal_uv_debug.
uniform int       uDebugUvVis;

out vec4 fragColor;
)glsl";

const char* kFragmentShaderMain = R"glsl(
void main()
{
    // Resolve base color: texture sample * factor, OR factor alone.
    vec4 baseColor = uBaseColorFactor;
    if (uHasBaseColorTex)
        baseColor *= texture(uBaseColorTex, vUV);

    // Alpha-test for Mask materials (hair cards eventually). Discard
    // happens BEFORE lighting math so we don't shade pixels that get
    // thrown away.
    if (uAlphaCutoff > 0.0 && baseColor.a < uAlphaCutoff)
        discard;

    vec3 N = normalize(vNormalWorld);
    vec3 L = normalize(uSunDir);
    float halfL = dot(N, L) * 0.5 + 0.5;
    float shadow = sampleSunShadow(vWorldPos, N);
    // Match the world's lighting model: hemispheric ambient
    // (kSkyAmbient/kGroundAmbient blended by surface up-facing) plus
    // a kSunTint-colored directional term gated by half-Lambert +
    // shadow. Without this, characters got a flat white-cream and
    // read as cut-out against the warm-lit terrain around them.
    vec3 N_up = vec3(0.0, 1.0, 0.0);
    float skyFactor = dot(N, N_up) * 0.5 + 0.5;
    vec3 ambient = mix(kGroundAmbient, kSkyAmbient, skyFactor);
    vec3 lit = ambient + kSunTint * halfL * shadow;
    // Compose base albedo based on tint mode. Multiply mode multiplies
    // the sample against tint per channel (subtle re-tinting).
    // Colorize mode replaces the sample's chroma with the tint's while
    // preserving luminance detail -- the Souls hair-colour convention.
    vec3 albedo;
    if (uTintMode == 1)
    {
        // Rec. 709 luminance carries the diffuse's strand light/dark
        // detail. Real hair diffuses ship MUCH darker than intuition
        // suggests: measurement of short01 finds a linear-luminance
        // median of ~0.012 (nearly black) with only the top ~10% of
        // pixels above 0.05. A naive `lum * tint` -- or even a smooth
        // remap of [0.03, 0.55] -- crushes the bottom 75% of pixels
        // to black regardless of tint. Souls hair, in contrast, wants
        // the base to READ as the picked colour with strand detail
        // gently modulating.
        //
        // Fix: gamma-curve remap. pow(lum, 0.3) lifts the median
        // (0.012 -> 0.28) and P75 (0.015 -> 0.31) into a bright base
        // range while still preserving contrast between darkest
        // shadows and brightest highlights. Then multiply by tint so
        // the tint IS the hair colour. Constant 0.3 was picked to hit
        // "median brightness ~= 0.3 for a linear luminance of 0.01",
        // which reads as a full-strength hair colour post-lighting.
        float lum = dot(baseColor.rgb, vec3(0.2126, 0.7152, 0.0722));
        float shade = pow(clamp(lum, 0.0, 1.0), 0.3);
        albedo = shade * uTint;
    }
    else
    {
        albedo = baseColor.rgb * uTint;
    }
    // Tonemap linear radiance through exposure-scaled Reinhard so
    // HDR sun term + uTint values don't clamp at 1.0 before display.
    // uTint is linear-space (the C++ side linearizes appearance.color
    // at load).
    vec3 c = tonemap(lit * albedo);

    // Diagnostic: re-color near-red pixels with their UV. Output channels:
    //   .r = vUV.x  (0..1 across the diffuse horizontally)
    //   .g = vUV.y  (0..1 across the diffuse vertically)
    //   .b = 0
    // So a red-eye pixel that samples UV (0.04, 0.41) renders as
    // (10, 105, 0) -- a dark green. UV (0.85, 0.50) renders as
    // (217, 128, 0) -- orange. Any UV with x near 1.0 looks red
    // (so we can still tell those from "real" red bug pixels).
    // Also: paint pixels that sample the suspected placeholder iris
    // regions (red blobs) in bright cyan for instant identification.
    if (uDebugUvVis != 0)
    {
        // Detect post-lighting near-red using sRGB-encoded color (since
        // that's what reads "red" on the user's screen). Approximate:
        // linear c near (1,0,0) -- threshold pre-tonemap.
        bool nearRed = c.r > 0.45 && c.g < 0.20 && c.b < 0.20;
        // Detect samples of the two placeholder iris blob UV regions
        // on the body diffuse (from authoring-pack inspection).
        bool inLeftBlob1 = vUV.x > 0.00 && vUV.x < 0.08 && vUV.y > 0.36 && vUV.y < 0.46;
        bool inLeftBlob2 = vUV.x > 0.08 && vUV.x < 0.17 && vUV.y > 0.36 && vUV.y < 0.46;
        bool inBottomRightBlob = vUV.x > 0.85 && vUV.y > 0.92;
        if (inLeftBlob1) c = vec3(0.0, 1.0, 1.0);            // cyan
        else if (inLeftBlob2) c = vec3(0.0, 0.5, 1.0);       // blue-cyan
        else if (inBottomRightBlob) c = vec3(1.0, 0.0, 1.0); // magenta
        else if (nearRed) c = vec3(vUV.x, vUV.y, 0.0);
    }

    fragColor = vec4(c, uAlpha * baseColor.a);
}
)glsl";

// Module-state for the program + cached uniform locations. One program
// shared across all skinned draws.
GLuint sProgram = 0;
GLuint sBoneSsbo = 0; // shader-storage buffer holding the bone palette
                      // for the current draw. Reused across all draws --
                      // glBufferData with the per-actor palette per call.
GLint sUniModel = -1;
GLint sUniViewProj = -1;
GLint sUniTint = -1;
GLint sUniSunDir = -1;
GLint sUniAlpha = -1;
GLint sUniShadowMap = -1;
GLint sUniLightViewProj = -1;
GLint sUniShadowSunDir = -1;
GLint sUniShadowCamPos = -1;
GLint sUniMorphCount = -1;
GLint sUniVertexCount = -1;
GLint sUniMorphWeights = -1; // base location of the float[32] array
GLint sUniExposure = -1;
GLint sUniKSkyAmbient = -1;
GLint sUniKGroundAmbient = -1;
GLint sUniKSunTint = -1;
// Per-material uniforms. Bound per primitive in the draw loop.
GLint sUniBaseColorTex = -1;
GLint sUniHasBaseColorTex = -1;
GLint sUniBaseColorFactor = -1;
GLint sUniAlphaCutoff = -1;
GLint sUniTintMode = -1;
GLint sUniDebugUvVis = -1;

// Per-frame tint mode. Set by setSkeletalTintMode() before a draw
// call and cleared back to Multiply after each draw so the default
// (body/eye) behaviour survives without every caller having to
// remember to reset it. HairRenderer flips this to Colorize just
// before its drawSkeletalMesh call.
int sFrameTintMode = 0; // 0 = Multiply, 1 = Colorize

// Per-frame eye colour. When set (has_eye_tint = true), any primitive
// whose material.role == Eyes gets this tint in Colorize mode instead
// of the actor's body tint. Reset per drawSkeletalMesh call so eyes
// only affect the specific actor draw the caller intended.
glm::vec3 sFrameEyeTint(1.0f);
bool sFrameHasEyeTint = false;

// Texture unit reserved for the per-material baseColorTex sample.
// Skeletal pass uses unit 0 for the material; shadow map sampler
// is configured separately to a different unit by the depth pass.
constexpr int kBaseColorTexUnit = 0;

glm::vec3 sFrameSunDir(0.0f, 1.0f, 0.0f);
glm::mat4 sFrameLightVP(1.0f);
glm::vec3 sFrameShadowCamPos(0.0f);
int sFrameShadowUnit = 1;
float sFrameExposure = 1.0f;

// Ambient + sun_tint override. When sAmbientOverrideActive is true,
// beginSkeletalPass pushes these values instead of pulling from
// Tunables.lighting. Used by the character preview to apply a
// softer portrait fill against an empty FBO background.
bool sAmbientOverrideActive = false;
glm::vec3 sAmbientOverrideSky(0.0f);
glm::vec3 sAmbientOverrideGround(0.0f);
glm::vec3 sAmbientOverrideSunTint(0.0f);

} // namespace

bool initSkeletalRenderer()
{
    if (sProgram != 0)
        return true; // idempotent

    using namespace selva::render;
    const std::string fs = std::string(kFragmentShaderCore) + kAmbientConstantsGLSL + kShadowGLSL +
                           kTonemapGLSL + kFragmentShaderMain;
    sProgram = engine::gl::compileProgram(kVertexShader, fs.c_str());
    if (sProgram == 0)
    {
        std::fprintf(stderr, "[SkeletalRenderer] shader compile/link failed\n");
        return false;
    }
    sUniModel = glGetUniformLocation(sProgram, "uModel");
    sUniViewProj = glGetUniformLocation(sProgram, "uViewProj");
    sUniTint = glGetUniformLocation(sProgram, "uTint");
    sUniSunDir = glGetUniformLocation(sProgram, "uSunDir");
    sUniAlpha = glGetUniformLocation(sProgram, "uAlpha");
    sUniShadowMap = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProj = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDir = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPos = glGetUniformLocation(sProgram, "uShadowCameraPos");
    sUniMorphCount = glGetUniformLocation(sProgram, "uMorphCount");
    sUniVertexCount = glGetUniformLocation(sProgram, "uVertexCount");
    sUniMorphWeights = glGetUniformLocation(sProgram, "uMorphWeights[0]");
    sUniExposure = glGetUniformLocation(sProgram, "uExposure");
    sUniKSkyAmbient = glGetUniformLocation(sProgram, "uKSkyAmbient");
    sUniKGroundAmbient = glGetUniformLocation(sProgram, "uKGroundAmbient");
    sUniKSunTint = glGetUniformLocation(sProgram, "uKSunTint");
    sUniBaseColorTex = glGetUniformLocation(sProgram, "uBaseColorTex");
    sUniHasBaseColorTex = glGetUniformLocation(sProgram, "uHasBaseColorTex");
    sUniBaseColorFactor = glGetUniformLocation(sProgram, "uBaseColorFactor");
    sUniAlphaCutoff = glGetUniformLocation(sProgram, "uAlphaCutoff");
    sUniTintMode = glGetUniformLocation(sProgram, "uTintMode");
    sUniDebugUvVis = glGetUniformLocation(sProgram, "uDebugUvVis");

    glGenBuffers(1, &sBoneSsbo);
    return true;
}

void setSkeletalSun(const glm::vec3& sun_dir)
{
    sFrameSunDir = sun_dir;
}

void setSkeletalExposure(float exposure)
{
    sFrameExposure = exposure;
}

void setSkeletalShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                       const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    sFrameLightVP = light_view_proj;
    sFrameShadowUnit = shadow_texture_unit;
    sFrameSunDir = sun_dir;
    sFrameShadowCamPos = shadow_cam_pos;
}

void setSkeletalAmbientOverride(const glm::vec3& sky_ambient, const glm::vec3& ground_ambient,
                                const glm::vec3& sun_tint)
{
    sAmbientOverrideActive = true;
    sAmbientOverrideSky = sky_ambient;
    sAmbientOverrideGround = ground_ambient;
    sAmbientOverrideSunTint = sun_tint;
}

void clearSkeletalAmbientOverride()
{
    sAmbientOverrideActive = false;
}

void setSkeletalTintMode(TintMode mode)
{
    sFrameTintMode = static_cast<int>(mode);
}

void setSkeletalEyeTint(const glm::vec3& linear_rgb)
{
    sFrameEyeTint = linear_rgb;
    sFrameHasEyeTint = true;
}

void shutdownSkeletalRenderer()
{
    if (sBoneSsbo != 0)
    {
        glDeleteBuffers(1, &sBoneSsbo);
        sBoneSsbo = 0;
    }
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
}

void beginSkeletalPass()
{
    if (sProgram == 0)
        return;
    glUseProgram(sProgram);
    // Ambient + sun tint: Tunables.lighting by default, OR the
    // portrait-fill override if the caller (CharacterPreview) set
    // one. Override stays sticky until clearSkeletalAmbientOverride.
    glm::vec3 sky, ground, tint;
    if (sAmbientOverrideActive)
    {
        sky = sAmbientOverrideSky;
        ground = sAmbientOverrideGround;
        tint = sAmbientOverrideSunTint;
    }
    else
    {
        const auto& L = selva::tuning::current().lighting;
        sky = L.sky_ambient;
        ground = L.ground_ambient;
        tint = L.sun_tint;
    }
    if (sUniKSkyAmbient >= 0)
        glUniform3f(sUniKSkyAmbient, sky.x, sky.y, sky.z);
    if (sUniKGroundAmbient >= 0)
        glUniform3f(sUniKGroundAmbient, ground.x, ground.y, ground.z);
    if (sUniKSunTint >= 0)
        glUniform3f(sUniKSunTint, tint.x, tint.y, tint.z);
    // Skeletal meshes from third-party sources can't be assumed to
    // have consistently outward-wound normals -- back-face culling
    // makes parts of those rigs see-through at some angles. Disable
    // culling for the whole pass; correctness > fill rate. Static
    // meshes get culling back at endSkeletalPass.
    glDisable(GL_CULL_FACE);
}

void endSkeletalPass()
{
    // Restore the state the rest of the frame expects: cull-back ON,
    // blend OFF, depth-mask ON, no program bound.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint,
                      float alpha, const std::vector<float>& morph_weights)
{
    if (sProgram == 0 || !mesh.isLoaded())
        return;
    if (alpha <= 0.001f)
        return;

    // Per-actor uniforms: same for every primitive in this mesh.
    glUniformMatrix4fv(sUniModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(sUniViewProj, 1, GL_FALSE, glm::value_ptr(view_proj));
    glUniform3fv(sUniTint, 1, glm::value_ptr(tint));
    glUniform3fv(sUniSunDir, 1, glm::value_ptr(sFrameSunDir));
    glUniform1f(sUniAlpha, alpha);
    // Capture per-actor tint mode into a local so the per-primitive
    // loop below can re-upload it after any eye-primitive override
    // without reading zeroed module state.
    const int actor_tint_mode = sFrameTintMode;
    glUniform1i(sUniTintMode, actor_tint_mode);
    // Auto-reset the module-level knobs after upload so a subsequent
    // draw defaults back to Multiply / no eye override; callers who
    // want Colorize / eye tint set them right before their
    // drawSkeletalMesh call. Keeps state opt-in per draw.
    sFrameTintMode = 0;
    const bool actor_has_eye_tint = sFrameHasEyeTint;
    const glm::vec3 actor_eye_tint = sFrameEyeTint;
    sFrameHasEyeTint = false;
    glUniform1f(sUniExposure, sFrameExposure);
    glUniformMatrix4fv(sUniLightViewProj, 1, GL_FALSE, glm::value_ptr(sFrameLightVP));
    glUniform3fv(sUniShadowSunDir, 1, glm::value_ptr(sFrameSunDir));
    glUniform3fv(sUniShadowCamPos, 1, glm::value_ptr(sFrameShadowCamPos));
    glUniform1i(sUniShadowMap, sFrameShadowUnit);
    glUniform1i(sUniDebugUvVis, selva::debug::flags().skeletal_uv_debug ? 1 : 0);

    // Bone palette: one upload per actor, reused across primitives.
    if (!bone_palette.empty())
    {
        const GLsizeiptr bytes = static_cast<GLsizeiptr>(bone_palette.size() * sizeof(glm::mat4));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sBoneSsbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, glm::value_ptr(bone_palette[0]),
                     GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBonePaletteBinding, sBoneSsbo);
    }

    // Per-draw blend state.
    const bool blend_needed = (alpha < 0.999f);
    if (blend_needed)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    // Per-primitive: bind material, push morph, draw.
    for (const auto& prim : mesh.primitives)
    {
        if (prim.vao == 0 || prim.index_count == 0)
            continue;

        // Material binding: pull from mesh.materials[prim.material_index],
        // or fall back to a default-constructed Material when the
        // primitive has no material (legacy meshes / future asset
        // pipelines that don't author a material per primitive).
        Material defaultMat;
        const Material& m = (prim.material_index >= 0 &&
                             prim.material_index < static_cast<int>(mesh.materials.size()))
                                ? mesh.materials[prim.material_index]
                                : defaultMat;

        glUniform4fv(sUniBaseColorFactor, 1, glm::value_ptr(m.base_color_factor));
        if (m.base_color_tex != 0)
        {
            glActiveTexture(GL_TEXTURE0 + kBaseColorTexUnit);
            glBindTexture(GL_TEXTURE_2D, m.base_color_tex);
            glUniform1i(sUniBaseColorTex, kBaseColorTexUnit);
            glUniform1i(sUniHasBaseColorTex, 1);
        }
        else
        {
            glUniform1i(sUniHasBaseColorTex, 0);
        }
        glUniform1f(sUniAlphaCutoff,
                    m.alpha_mode == Material::AlphaMode::Mask ? m.alpha_cutoff : 0.0f);

        // Per-primitive tint override. Eye primitives get eye_tint in
        // Colorize mode so the player picks any iris colour and lands
        // on it regardless of the baked iris texture's original hue.
        // Other primitives keep the actor tint + mode already uploaded
        // per-actor above. Since sUniTint / sUniTintMode are uploaded
        // once per drawSkeletalMesh and again HERE per primitive, we
        // must restore them at the end of the loop iteration for the
        // NEXT primitive (which may not be an eye) to see the correct
        // actor-level values. Simpler: re-upload the correct value on
        // both entry and exit of the override.
        if (actor_has_eye_tint && m.role == Material::Role::Eyes)
        {
            glUniform3fv(sUniTint, 1, glm::value_ptr(actor_eye_tint));
            glUniform1i(sUniTintMode, 1); // Colorize
        }
        else
        {
            glUniform3fv(sUniTint, 1, glm::value_ptr(tint));
            glUniform1i(sUniTintMode, actor_tint_mode);
        }

        int active_morphs = 0;
        if (prim.morph_target_count > 0 && !morph_weights.empty())
        {
            active_morphs = std::min({prim.morph_target_count,
                                      static_cast<int>(morph_weights.size()), kMaxMorphWeights});
            if (active_morphs > 0)
            {
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMorphDeltasBinding,
                                 prim.morph_delta_ssbo);
                glUniform1fv(sUniMorphWeights, active_morphs, morph_weights.data());
                glUniform1i(sUniVertexCount, prim.vertex_count);
            }
        }
        glUniform1i(sUniMorphCount, active_morphs);

        glBindVertexArray(prim.vao);
        glDrawElements(GL_TRIANGLES, prim.index_count, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
}

} // namespace selva::anim
