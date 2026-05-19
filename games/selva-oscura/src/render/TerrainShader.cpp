#include "render/TerrainShader.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <glad/glad.h>

#include <cmath>
#include <string>

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
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
)glsl";

const char* kTerrainFSMain = R"glsl(
void main()
{
    // Surface lit by sun via half-Lambert against the heightfield's
    // computed normals. Then atmospheric in-scatter blends with the
    // shaded color according to view-distance, same as trees + sky.
    float halfL = dot(vNormal, uSunDir) * 0.5 + 0.5;
    vec3 ambient = vec3(0.15, 0.18, 0.24);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    vec3 surface = uBaseColor * (ambient + sunTint * halfL);

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
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;

} // namespace

bool initTerrainShader()
{
    const std::string fs = std::string(kTerrainFSCore) + kAtmosphereGLSL + kTerrainFSMain;
    sProgram = engine::gl::compileProgram(kTerrainVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    return true;
}

void shutdownTerrainShader()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniViewProjLoc = sUniBaseColorLoc = sUniSunDirLoc = sUniSunIntensityLoc = sUniCamPosLoc =
        sUniExposureLoc = -1;
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

} // namespace selva::render
