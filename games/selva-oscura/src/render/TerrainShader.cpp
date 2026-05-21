#include "render/TerrainShader.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"
#include "render/ShadowShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <string>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Vertices already in world space (Terrain.cpp bakes world positions).
const char* kTerrainVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vNormal = normalize(aNormal);
    vUV = aUV;
}
)glsl";

const char* kTerrainFSCore = R"glsl(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 fragColor;

uniform vec3 uBaseColor;
uniform vec3 uDarkLoam;
uniform vec3 uDryDirt;
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
)glsl";

const char* kTerrainFSMain = R"glsl(
// Cheap hash-based 3D noise. No texture upload; uses fract(sin(dot(.)))
// pattern, two octaves for coarse + fine detail.
float hash13(vec3 p)
{
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z);
}

void main()
{
    // Two-octave noise sampled at world XZ (Y omitted so the texture
    // doesn't smear vertically up cliffs). Coarse 5m features + fine
    // 0.7m mottling, weighted 0.7 / 0.3.
    float n_coarse = vnoise(vec3(vWorldPos.x * 0.2, 0.0, vWorldPos.z * 0.2));
    float n_fine = vnoise(vec3(vWorldPos.x * 1.4, 0.0, vWorldPos.z * 1.4));
    float n = mix(n_coarse, n_fine, 0.3);

    // Slope (0 flat, 1 vertical) — slopes show drier exposed dirt.
    float slope = clamp(1.0 - vNormal.y, 0.0, 1.0);
    float dryness = clamp(slope * 0.7 + (n - 0.5) * 0.6 + 0.15, 0.0, 1.0);
    vec3 dirt = mix(uDarkLoam, uDryDirt, dryness);

    // Sun lighting (half-Lambert + ambient + sun tint), attenuated
    // by the directional-light shadow map sample.
    float halfL = dot(vNormal, uSunDir) * 0.5 + 0.5;
    vec3 ambient = vec3(0.15, 0.18, 0.24);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    float shadow = sampleSunShadow(vWorldPos, vNormal);
    vec3 surface = dirt * (ambient + sunTint * halfL * shadow);

    vec3 viewVec = vWorldPos - uCamPos;
    float dist = length(viewVec);
    vec3 rayDir = viewVec / max(dist, 1e-4);
    vec3 transmittance;
    vec3 inScatter = atmosphereWithT(uCamPos, rayDir, uSunDir, uSunIntensity,
                                     dist, transmittance);

    vec3 col = surface * transmittance + inScatter;
    col = applyDistanceFog(col, rayDir, uSunDir, dist);
    col = col * uExposure;
    col = col / (col + vec3(1.0));
    fragColor = vec4(col, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLint sUniViewProjLoc = -1;
GLint sUniBaseColorLoc = -1;
GLint sUniDarkLoamLoc = -1;
GLint sUniDryDirtLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;
GLint sUniShadowMapLoc = -1;
GLint sUniLightViewProjLoc = -1;
GLint sUniShadowSunDirLoc = -1;
GLint sUniShadowCamPosLoc = -1;

} // namespace

bool initTerrainShader()
{
    const std::string fs = std::string(kTerrainFSCore) + kAtmosphereGLSL + kShadowGLSL +
                           kTerrainFSMain;
    sProgram = engine::gl::compileProgram(kTerrainVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniDarkLoamLoc = glGetUniformLocation(sProgram, "uDarkLoam");
    sUniDryDirtLoc = glGetUniformLocation(sProgram, "uDryDirt");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    sUniShadowMapLoc = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProjLoc = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDirLoc = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPosLoc = glGetUniformLocation(sProgram, "uShadowCameraPos");
    return true;
}

void shutdownTerrainShader()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniViewProjLoc = sUniBaseColorLoc = sUniDarkLoamLoc = sUniDryDirtLoc = sUniSunDirLoc =
        sUniSunIntensityLoc = sUniCamPosLoc = sUniExposureLoc = -1;
}

void useTerrainShader()
{
    glUseProgram(sProgram);
}

void setTerrainViewProj(const glm::mat4& view_proj)
{
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
}

void setTerrainAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
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

void setTerrainBaseColor(const glm::vec3& color)
{
    glUniform3f(sUniBaseColorLoc, color.x, color.y, color.z);
}

void setTerrainTones(const glm::vec3& dark_loam, const glm::vec3& dry_dirt)
{
    glUniform3f(sUniDarkLoamLoc, dark_loam.x, dark_loam.y, dark_loam.z);
    glUniform3f(sUniDryDirtLoc, dry_dirt.x, dry_dirt.y, dry_dirt.z);
}

void setTerrainShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                      const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    glUniformMatrix4fv(sUniLightViewProjLoc, 1, GL_FALSE, glm::value_ptr(light_view_proj));
    glUniform3f(sUniShadowSunDirLoc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform3f(sUniShadowCamPosLoc, shadow_cam_pos.x, shadow_cam_pos.y, shadow_cam_pos.z);
    glUniform1i(sUniShadowMapLoc, shadow_texture_unit);
}

} // namespace selva::render
