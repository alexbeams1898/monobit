#include "render/RegionShaders.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"
#include "render/ShadowShader.h"
#include "world/Lights.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

const char* kSceneVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aShade;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uViewProj;
uniform vec3 uCamPos;

out float vShade;
out vec3 vWorldPos;
out vec3 vViewVec;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uViewProj * worldPos;
    vShade = aShade;
    vWorldPos = worldPos.xyz;
    vViewVec = vWorldPos - uCamPos;
}
)glsl";

const char* kSceneFragmentShaderCore = R"glsl(
#version 330 core
in float vShade;
in vec3 vWorldPos;
in vec3 vViewVec;
out vec4 fragColor;

uniform float uTint;
uniform vec3 uBaseColor;  // per-primitive RGB color factor (1,1,1 = no tint)
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
uniform float uFlatShading; // 1.0 = output flat uBaseColor (bisect debug); 0.0 = full lighting

// Point lights. Same packing + cap as TerrainShader so the same light
// set works across both programs without divergence. See TerrainShader
// for the rationale on the cap.
#define MAX_LIGHTS 64
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform int uLightCount;
)glsl";

// surface lit by sun via half-Lambert; in-scattered atmosphere
// along the view ray + transmittance gives aerial perspective.
const char* kSceneFragmentShaderMain = R"glsl(
void main()
{
    // Bisect-debug: short-circuit to flat color if requested. Tests
    // whether flicker is caused by anything downstream (normal,
    // lighting, shadow, atmosphere, exposure, tonemap). If flicker
    // disappears here, the cause is shader math (likely dFdx/dFdy
    // normal flip). If flicker remains, cause is upstream (geometry /
    // depth precision / MSAA / something else).
    if (uFlatShading > 0.5)
    {
        fragColor = vec4(uBaseColor, 1.0);
        return;
    }
    vec3 N = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    float halfL = dot(N, uSunDir) * 0.5 + 0.5;
    float g = clamp(vShade * uTint, 0.0, 1.0);

    // Hemispheric ambient: dot(N, up)-driven blend between a sky
    // tint and a ground bounce tint. Up-facing surfaces read sky,
    // down-facing read ground, side-facing average. Restores per-
    // face variation in shadowed regions where the sun term is zero.
    vec3 N_up = vec3(0.0, 1.0, 0.0);
    float skyFactor = dot(N, N_up) * 0.5 + 0.5;
    vec3 skyAmbient    = vec3(0.18, 0.22, 0.28);  // overcast bluish overhead
    vec3 groundAmbient = vec3(0.08, 0.06, 0.05);  // dim warm dirt bounce
    vec3 ambient = mix(groundAmbient, skyAmbient, skyFactor);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    float shadow = sampleSunShadow(vWorldPos, N);

    vec3 pointLight = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i)
    {
        vec3 toLight = uLightPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = uLightPosRadius[i].w;
        if (radius <= 0.0 || dist >= radius)
            continue;
        vec3 ldir = toLight / max(dist, 1e-4);
        float ndotl = dot(N, ldir) * 0.5 + 0.5;
        float falloff = 1.0 - smoothstep(0.0, radius, dist);
        pointLight += uLightColorIntensity[i].rgb * uLightColorIntensity[i].w
                      * (ndotl * falloff);
    }

    vec3 surface = uBaseColor * g * (ambient + sunTint * halfL * shadow + pointLight);

    // Aerial perspective: in-scatter + transmittance along view ray
    // from camera to this fragment. Zeroed when the camera is indoors
    // — the atmospheric scatter math doesn't know about wall
    // occlusion, so it would paint sky light onto enclosed surfaces.
    float dist = length(vViewVec);
    vec3 rayDir = vViewVec / dist;
    vec3 transmittance;
    vec3 inScatter = atmosphereWithT(uCamPos, rayDir, uSunDir, uSunIntensity,
                                     dist, transmittance);

    vec3 col = surface * transmittance + inScatter;

    col = col * uExposure;
    col = col / (col + vec3(1.0));
    fragColor = vec4(col, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLint sUniModelLoc = -1;
GLint sUniViewLoc = -1;
GLint sUniViewProjLoc = -1;
GLint sUniTintLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniCamPosVSLoc = -1;
GLint sUniExposureLoc = -1;
GLint sUniShadowMapLoc = -1;
GLint sUniLightViewProjLoc = -1;
GLint sUniShadowSunDirLoc = -1;
GLint sUniShadowCamPosLoc = -1;
GLint sUniFlatShadingLoc = -1;
GLint sUniBaseColorLoc = -1;
GLint sUniLightPosRadiusLoc = -1;
GLint sUniLightColorIntensityLoc = -1;
GLint sUniLightCountLoc = -1;

constexpr int kMaxLights = 64;
bool sLightOverflowWarned = false;

} // namespace

bool initRegionProgram()
{
    const std::string fs = std::string(kSceneFragmentShaderCore) + kAtmosphereGLSL + kShadowGLSL +
                           kSceneFragmentShaderMain;
    sProgram = engine::gl::compileProgram(kSceneVertexShader, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniModelLoc = glGetUniformLocation(sProgram, "uModel");
    sUniViewLoc = glGetUniformLocation(sProgram, "uView");
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniTintLoc = glGetUniformLocation(sProgram, "uTint");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniCamPosVSLoc = sUniCamPosLoc;
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    sUniShadowMapLoc = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProjLoc = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDirLoc = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPosLoc = glGetUniformLocation(sProgram, "uShadowCameraPos");
    sUniFlatShadingLoc = glGetUniformLocation(sProgram, "uFlatShading");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniLightPosRadiusLoc = glGetUniformLocation(sProgram, "uLightPosRadius");
    sUniLightColorIntensityLoc = glGetUniformLocation(sProgram, "uLightColorIntensity");
    sUniLightCountLoc = glGetUniformLocation(sProgram, "uLightCount");
    return true;
}

void shutdownRegionProgram()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniModelLoc = sUniViewLoc = sUniViewProjLoc = sUniTintLoc = sUniSunDirLoc =
        sUniSunIntensityLoc = sUniCamPosLoc = sUniCamPosVSLoc = sUniExposureLoc = -1;
}

void useRegionProgram()
{
    glUseProgram(sProgram);
    if (sUniBaseColorLoc >= 0)
        glUniform3f(sUniBaseColorLoc, 1.0f, 1.0f, 1.0f);
}

void setSceneView(const glm::mat4& view)
{
    glUniformMatrix4fv(sUniViewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

void setSceneViewProj(const glm::mat4& view_proj)
{
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
}

void setSceneModel(const glm::mat4& model)
{
    glUniformMatrix4fv(sUniModelLoc, 1, GL_FALSE, glm::value_ptr(model));
}

void setSceneTint(float tint)
{
    glUniform1f(sUniTintLoc, tint);
}

void setSceneFlatShading(bool on)
{
    if (sUniFlatShadingLoc >= 0)
        glUniform1f(sUniFlatShadingLoc, on ? 1.0f : 0.0f);
}

void setSceneBaseColor(const glm::vec3& rgb)
{
    if (sUniBaseColorLoc >= 0)
        glUniform3f(sUniBaseColorLoc, rgb.x, rgb.y, rgb.z);
}

void setSceneAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
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

void setScenePointLights(const std::vector<engine::world::LightSource>& lights)
{
    const int total = static_cast<int>(lights.size());
    const int n = std::min(total, kMaxLights);
    if (total > kMaxLights && !sLightOverflowWarned)
    {
        std::fprintf(stderr,
                     "[RegionShaders] light count %d exceeds MAX_LIGHTS=%d; "
                     "extras dropped. Raise the cap or filter by region.\n",
                     total, kMaxLights);
        sLightOverflowWarned = true;
    }

    float pos_radius[kMaxLights * 4];
    float color_intensity[kMaxLights * 4];
    for (int i = 0; i < n; ++i)
    {
        const auto& L = lights[static_cast<size_t>(i)];
        pos_radius[i * 4 + 0] = L.position.x;
        pos_radius[i * 4 + 1] = L.position.y;
        pos_radius[i * 4 + 2] = L.position.z;
        pos_radius[i * 4 + 3] = L.radius;
        color_intensity[i * 4 + 0] = L.color.x;
        color_intensity[i * 4 + 1] = L.color.y;
        color_intensity[i * 4 + 2] = L.color.z;
        color_intensity[i * 4 + 3] = L.intensity;
    }
    if (n > 0)
    {
        glUniform4fv(sUniLightPosRadiusLoc, n, pos_radius);
        glUniform4fv(sUniLightColorIntensityLoc, n, color_intensity);
    }
    glUniform1i(sUniLightCountLoc, n);
}

void setSceneShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                    const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    glUniformMatrix4fv(sUniLightViewProjLoc, 1, GL_FALSE, glm::value_ptr(light_view_proj));
    glUniform3f(sUniShadowSunDirLoc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform3f(sUniShadowCamPosLoc, shadow_cam_pos.x, shadow_cam_pos.y, shadow_cam_pos.z);
    glUniform1i(sUniShadowMapLoc, shadow_texture_unit);
}

} // namespace selva::render
