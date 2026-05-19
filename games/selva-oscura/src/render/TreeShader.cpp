#include "render/TreeShader.h"

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
    // Subtle breath-of-the-wood wind: vanishes at base, grows
    // quadratically toward canopy. Two frequencies layered (slow
    // lean + faster leaflet flutter) so it reads as living matter,
    // not a sine wave. Per-instance phase offset is provided by the
    // C++ side so the forest doesn't sway in unison.
    vec3 localPos = aPos;
    float h = max(localPos.y, 0.0);
    float heightWeight = h * h * 0.012;
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

uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
)glsl";

const char* kTreeFSMain = R"glsl(
void main()
{
    vec4 base = texture(uBaseColor, vUV);

    // Alpha-test for branch canopies. Trunks call with cutoff=0 so
    // this is a no-op.
    if (base.a < uAlphaCutoff) discard;

    float halfL = dot(normalize(vNormal), uSunDir) * 0.5 + 0.5;
    vec3 ambient = vec3(0.15, 0.18, 0.24);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    vec3 surface = base.rgb * (ambient + sunTint * halfL);

    // Aerial perspective (same atmosphere as scene + sky).
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
GLint sUniModelLoc = -1;
GLint sUniViewProjLoc = -1;
GLint sUniViewLoc = -1;
GLint sUniTimeLoc = -1;
GLint sUniWindPhaseLoc = -1;
GLint sUniBaseColorLoc = -1;
GLint sUniAlphaCutoffLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;

} // namespace

bool initTreeShader()
{
    const std::string fs = std::string(kTreeFSCore) + kAtmosphereGLSL + kTreeFSMain;
    sProgram = engine::gl::compileProgram(kTreeVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniModelLoc = glGetUniformLocation(sProgram, "uModel");
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniTimeLoc = glGetUniformLocation(sProgram, "uTime");
    sUniWindPhaseLoc = glGetUniformLocation(sProgram, "uWindPhase");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniAlphaCutoffLoc = glGetUniformLocation(sProgram, "uAlphaCutoff");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");

    glUseProgram(sProgram);
    glUniform1i(sUniBaseColorLoc, 0);
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
        sUniBaseColorLoc = sUniAlphaCutoffLoc = sUniSunDirLoc = sUniSunIntensityLoc =
            sUniCamPosLoc = sUniExposureLoc = -1;
}

void useTreeShader()
{
    glUseProgram(sProgram);
}

void setTreeView(const glm::mat4& /*view*/) {} // unused (kept for API symmetry)

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

void setTreeTime(float t)
{
    glUniform1f(sUniTimeLoc, t);
}

} // namespace selva::render
