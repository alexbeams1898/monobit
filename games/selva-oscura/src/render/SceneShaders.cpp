#include "render/SceneShaders.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
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
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
)glsl";

// surface lit by sun via half-Lambert; in-scattered atmosphere
// along the view ray + transmittance gives aerial perspective.
const char* kSceneFragmentShaderMain = R"glsl(
void main()
{
    vec3 N = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    float halfL = dot(N, uSunDir) * 0.5 + 0.5;
    float g = clamp(vShade * uTint, 0.0, 1.0);

    // Sun-attenuated through the atmosphere from above (simple: use
    // the sun-aligned scattering color as the lit-surface tint).
    vec3 ambient = vec3(0.15, 0.18, 0.24);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    vec3 surface = vec3(g) * (ambient + sunTint * halfL);

    // Aerial perspective: in-scatter + transmittance along view ray
    // from camera to this fragment.
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

} // namespace

bool initSceneProgram()
{
    const std::string fs =
        std::string(kSceneFragmentShaderCore) + kAtmosphereGLSL + kSceneFragmentShaderMain;
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
    sUniCamPosVSLoc = sUniCamPosLoc; // same uniform, accessed in both stages
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    return true;
}

void shutdownSceneProgram()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniModelLoc = sUniViewLoc = sUniViewProjLoc = sUniTintLoc = sUniSunDirLoc =
        sUniSunIntensityLoc = sUniCamPosLoc = sUniCamPosVSLoc = sUniExposureLoc = -1;
}

void useSceneProgram()
{
    glUseProgram(sProgram);
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

} // namespace selva::render
