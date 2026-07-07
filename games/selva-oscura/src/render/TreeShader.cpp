#include "render/TreeShader.h"

#include "Tunables.h"
#include "gl/ShaderUtils.h"
#include "render/ActiveLightingEnv.h"
#include "render/AmbientConstants.h"
#include "render/AtmosphereShader.h"
#include "render/ShadowShader.h"
#include "render/TonemapShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <string>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

const char* kTreeVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform float uTime;
uniform float uWindPhase;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    // Subtle breath-of-the-wood wind: vanishes at base, grows with
    // height toward canopy. Two frequencies layered (slow lean +
    // faster leaflet flutter) so it reads as living matter, not a
    // sine wave. Per-instance phase offset is provided by the C++
    // side so the forest doesn't sway in unison.
    //
    // Height weight saturates at ~0.2 around h=18 (canopy top of the
    // tallest variant) so the tip of even a tall tree sways no more
    // than ~10cm. Without the cap, h*h grows quadratically and tall
    // tree canopies swing visibly far from rest.
    vec3 localPos = aPos;
    float h = max(localPos.y, 0.0);
    float heightWeight = min(h * h * 0.0006, 0.2);
    float slow = sin(uTime * 0.55 + uWindPhase) * heightWeight;
    float fast = sin(uTime * 2.7 + uWindPhase * 1.7 + localPos.x * 0.8) * heightWeight * 0.35;
    localPos.x += slow * 0.45 + fast;
    localPos.z += slow * 0.25 + fast * 0.6;

    vec4 worldPos = uModel * vec4(localPos, 1.0);
    gl_Position = uViewProj * worldPos;
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
}
)glsl";

const char* kTreeFSCore = R"glsl(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uBaseColor;
uniform float uAlphaCutoff;

// Per-frame foliage tint multiplier (vec3). Multiplied into base.rgb
// AFTER texture sample, BEFORE lighting. v1: hardcoded to a warm
// grey-brown at draw time to render the dead wood (per wood.md: the
// wood is dead at game start, healing as keepers fall + sangue
// leaks in). Identity = vec3(1.0) (default behavior, asset color
// preserved). Future: per-region / per-keeper-restoration value.
uniform vec3 uFoliageTint;

uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
// uExposure is declared inside kTonemapGLSL alongside tonemap().

// Point-light array. Same schema as terrain/static-mesh/skeletal
// shaders: xyz = world position, w = radius; rgb = linear color,
// w = intensity. Iterated per fragment against the CANOPY normal
// (up-vector); trees are treated as diffuse sky-facing volumes so
// point-light hit depends on distance not surface facing.
#define MAX_LIGHTS 32
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform int uLightCount;
)glsl";

const char* kTreeFSMain = R"glsl(
void main()
{
    vec4 base = texture(uBaseColor, vUV);

    // Alpha-test for branch canopies. Trunks call with cutoff=0 so
    // this is a no-op.
    if (base.a < uAlphaCutoff) discard;

    // Apply per-frame foliage tint BEFORE lighting (tint is a
    // base-color modifier; lighting math reads the tinted color).
    base.rgb *= uFoliageTint;

    // Foliage uses a hemispherical up-vector for lighting, not the
    // per-vertex card-aligned normal. Card normals at edge-on
    // viewing angles produce per-frame brightness flicker because
    // dot(N,L) sits near the steep part of cosine and rasterizer
    // interpolation noise gets amplified. Real leaves scatter
    // through the canopy anyway; an up-vector is more physically
    // accurate than card-normals for soft foliage AND immune to
    // per-frame interpolation noise. Standard production foliage
    // shading technique.
    const vec3 canopyN = vec3(0.0, 1.0, 0.0);
    float halfL = dot(canopyN, uSunDir) * 0.5 + 0.5;
    // Hemispheric ambient. With canopyN = up, this degenerates to
    // skyAmbient — canopy receives sky light directly.
    float skyFactor = dot(canopyN, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    // kSkyAmbient / kGroundAmbient / kSunTint -- from kAmbientConstantsGLSL.
    vec3 ambient = mix(kGroundAmbient, kSkyAmbient, skyFactor);
    float shadow = sampleSunShadow(vWorldPos, canopyN);

    // Point-light contribution against the canopy up-vector. Same
    // formula as terrain / static-mesh / skeletal. Trees close to a
    // torch pick up a warm wash on their sky-facing side.
    vec3 pointLight = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i)
    {
        vec3 toLight = uLightPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = uLightPosRadius[i].w;
        if (radius <= 0.0 || dist >= radius)
            continue;
        vec3 ldir = toLight / max(dist, 1e-4);
        float ndotl = dot(canopyN, ldir) * 0.5 + 0.5;
        // Inverse-square attenuation with smooth cutoff (see TerrainShader).
        float dr = dist / max(radius, 1e-4);
        float invSq = 1.0 / (1.0 + 2.0 * dr + dr * dr);
        float window = 1.0 - smoothstep(radius * 0.75, radius, dist);
        float falloff = invSq * window;
        pointLight += uLightColorIntensity[i].rgb * uLightColorIntensity[i].w
                      * (ndotl * falloff);
    }

    vec3 surface = base.rgb * (ambient + kSunTint * halfL * shadow + pointLight);

    // Aerial perspective (same atmosphere as region + sky).
    vec3 viewVec = vWorldPos - uCamPos;
    float dist = length(viewVec);
    vec3 rayDir = viewVec / max(dist, 1e-4);
    vec3 transmittance;
    vec3 inScatter = atmosphereWithT(uCamPos, rayDir, uSunDir, uSunIntensity,
                                     dist, transmittance);

    vec3 col = surface * transmittance + inScatter;
    col = applyDistanceFog(col, rayDir, uSunDir, dist);
    col = tonemap(col);
    fragColor = vec4(col, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLint sUniModelLoc = -1;
GLint sUniViewProjLoc = -1;
GLint sUniViewLoc = -1;
GLint sUniTimeLoc = -1;
GLint sUniWindPhaseLoc = -1;
GLint sUniBaseColorLoc = -1;
GLint sUniAlphaCutoffLoc = -1;
GLint sUniFoliageTintLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;
GLint sUniShadowMapLoc = -1;
GLint sUniLightViewProjLoc = -1;
GLint sUniShadowSunDirLoc = -1;
GLint sUniShadowCamPosLoc = -1;
GLint sUniKSkyAmbientLoc = -1;
GLint sUniKGroundAmbientLoc = -1;
GLint sUniKSunTintLoc = -1;
GLint sUniLightPosRadiusLoc = -1;
GLint sUniLightColorIntensityLoc = -1;
GLint sUniLightCountLoc = -1;

constexpr int kMaxLights = 32;
int sTreeLightCount = 0;
float sTreeLightPosRadius[kMaxLights * 4] = {};
float sTreeLightColorIntensity[kMaxLights * 4] = {};

} // namespace

bool initTreeShader()
{
    const std::string fs = std::string(kTreeFSCore) + kAmbientConstantsGLSL + kAtmosphereGLSL +
                           kShadowGLSL + kTonemapGLSL + kTreeFSMain;
    sProgram = engine::gl::compileProgram(kTreeVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniModelLoc = glGetUniformLocation(sProgram, "uModel");
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniTimeLoc = glGetUniformLocation(sProgram, "uTime");
    sUniWindPhaseLoc = glGetUniformLocation(sProgram, "uWindPhase");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniAlphaCutoffLoc = glGetUniformLocation(sProgram, "uAlphaCutoff");
    sUniFoliageTintLoc = glGetUniformLocation(sProgram, "uFoliageTint");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    sUniShadowMapLoc = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProjLoc = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDirLoc = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPosLoc = glGetUniformLocation(sProgram, "uShadowCameraPos");
    sUniKSkyAmbientLoc = glGetUniformLocation(sProgram, "uKSkyAmbient");
    sUniKGroundAmbientLoc = glGetUniformLocation(sProgram, "uKGroundAmbient");
    sUniKSunTintLoc = glGetUniformLocation(sProgram, "uKSunTint");
    sUniLightPosRadiusLoc = glGetUniformLocation(sProgram, "uLightPosRadius[0]");
    sUniLightColorIntensityLoc = glGetUniformLocation(sProgram, "uLightColorIntensity[0]");
    sUniLightCountLoc = glGetUniformLocation(sProgram, "uLightCount");

    glUseProgram(sProgram);
    glUniform1i(sUniBaseColorLoc, 0);
    // Identity foliage tint by default -- callers that don't set it
    // get asset-color-preserved trees. Dead-wood rendering calls
    // setTreeFoliageTint() per-frame to override.
    glUniform3f(sUniFoliageTintLoc, 1.0f, 1.0f, 1.0f);
    glUseProgram(0);
    return true;
}

void shutdownTreeShader()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniModelLoc = sUniViewProjLoc = sUniViewLoc = sUniTimeLoc = sUniWindPhaseLoc =
        sUniBaseColorLoc = sUniAlphaCutoffLoc = sUniFoliageTintLoc = sUniSunDirLoc =
            sUniSunIntensityLoc = sUniCamPosLoc = sUniExposureLoc = -1;
}

void useTreeShader()
{
    glUseProgram(sProgram);
    // Read the frame's active lighting env (resolved from camera XZ).
    // Foliage in the Wood picks up sunlit ambient; if a tree ever
    // grows in Limbo it'd pick up cavern ambient instead. Fall back
    // to Tunables when env isn't set yet.
    const auto& env = selva::render::activeLightingEnv();
    const auto& fallback = selva::tuning::current().lighting;
    const glm::vec3 sky =
        (env.sky_ambient.x + env.sky_ambient.y + env.sky_ambient.z) > 0.0f
            ? env.sky_ambient
            : fallback.sky_ambient;
    const glm::vec3 ground =
        (env.ground_ambient.x + env.ground_ambient.y + env.ground_ambient.z) > 0.0f
            ? env.ground_ambient
            : fallback.ground_ambient;
    const glm::vec3 tint =
        (env.sun_tint.x + env.sun_tint.y + env.sun_tint.z) > 0.0f
            ? env.sun_tint * env.sun_multiplier
            : fallback.sun_tint;
    if (sUniKSkyAmbientLoc >= 0)
        glUniform3f(sUniKSkyAmbientLoc, sky.x, sky.y, sky.z);
    if (sUniKGroundAmbientLoc >= 0)
        glUniform3f(sUniKGroundAmbientLoc, ground.x, ground.y, ground.z);
    if (sUniKSunTintLoc >= 0)
        glUniform3f(sUniKSunTintLoc, tint.x, tint.y, tint.z);
    // Push point lights (torches, campfires) accumulated by
    // setTreePointLights. Same schema as terrain / static-mesh /
    // skeletal shaders so a torch lights foliage the same way it
    // lights walls.
    if (sTreeLightCount > 0 && sUniLightPosRadiusLoc >= 0)
        glUniform4fv(sUniLightPosRadiusLoc, sTreeLightCount, sTreeLightPosRadius);
    if (sTreeLightCount > 0 && sUniLightColorIntensityLoc >= 0)
        glUniform4fv(sUniLightColorIntensityLoc, sTreeLightCount, sTreeLightColorIntensity);
    if (sUniLightCountLoc >= 0)
        glUniform1i(sUniLightCountLoc, sTreeLightCount);
}

void setTreePointLights(const std::vector<engine::world::LightSource>& lights)
{
    const int total = static_cast<int>(lights.size());
    const int n = std::min(total, kMaxLights);
    for (int i = 0; i < n; ++i)
    {
        const auto& L = lights[static_cast<size_t>(i)];
        sTreeLightPosRadius[i * 4 + 0] = L.position.x;
        sTreeLightPosRadius[i * 4 + 1] = L.position.y;
        sTreeLightPosRadius[i * 4 + 2] = L.position.z;
        sTreeLightPosRadius[i * 4 + 3] = L.radius;
        sTreeLightColorIntensity[i * 4 + 0] = L.color.x;
        sTreeLightColorIntensity[i * 4 + 1] = L.color.y;
        sTreeLightColorIntensity[i * 4 + 2] = L.color.z;
        sTreeLightColorIntensity[i * 4 + 3] = L.intensity;
    }
    sTreeLightCount = n;
}

void setTreeView(const glm::mat4& /*view*/)
{
} // unused (kept for API symmetry)

void setTreeViewProj(const glm::mat4& view_proj)
{
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
}

void setTreeModel(const glm::mat4& model)
{
    glUniformMatrix4fv(sUniModelLoc, 1, GL_FALSE, glm::value_ptr(model));
}

void setTreeWindPhase(float phase)
{
    glUniform1f(sUniWindPhaseLoc, phase);
}

void setTreeAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                       const glm::vec3& cam_pos, float exposure)
{
    const float len =
        std::sqrt(sun_dir.x * sun_dir.x + sun_dir.y * sun_dir.y + sun_dir.z * sun_dir.z);
    const float inv = (len > 1e-6f) ? (1.0f / len) : 1.0f;
    glUniform3f(sUniSunDirLoc, sun_dir.x * inv, sun_dir.y * inv, sun_dir.z * inv);
    glUniform3f(sUniSunIntensityLoc, sun_intensity.x, sun_intensity.y, sun_intensity.z);
    glUniform3f(sUniCamPosLoc, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform1f(sUniExposureLoc, exposure);
}

void setTreeBaseColor(std::uint32_t tex)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void setTreeAlphaCutoff(float cutoff)
{
    glUniform1f(sUniAlphaCutoffLoc, cutoff);
}

void setTreeFoliageTint(const glm::vec3& tint)
{
    glUniform3f(sUniFoliageTintLoc, tint.x, tint.y, tint.z);
}

void setTreeTime(float t)
{
    glUniform1f(sUniTimeLoc, t);
}

void setTreeShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                   const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    glUniformMatrix4fv(sUniLightViewProjLoc, 1, GL_FALSE, glm::value_ptr(light_view_proj));
    glUniform3f(sUniShadowSunDirLoc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform3f(sUniShadowCamPosLoc, shadow_cam_pos.x, shadow_cam_pos.y, shadow_cam_pos.z);
    glUniform1i(sUniShadowMapLoc, shadow_texture_unit);
}

} // namespace selva::render
