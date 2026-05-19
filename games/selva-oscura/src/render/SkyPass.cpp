#include "render/SkyPass.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"
#include "render/Camera.h"

#include <glm/gtc/type_ptr.hpp>

#include <glad/glad.h>

#include <cmath>
#include <string>

namespace selva::render
{

namespace
{

constexpr float kQuadVerts[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};

const char* kSkyVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// Sky = atmosphere() evaluated on an infinite-distance view ray.
// Sun disc is the brightest 0.5° around uSunDir, added on top of the
// scattered radiance (which already produces the Mie halo).
const char* kSkyFSCore = R"glsl(
#version 330 core
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform vec2 uViewport;
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;
)glsl";

const char* kSkyFSMain = R"glsl(
void main()
{
    // Per-fragment ray reconstruction from gl_FragCoord. This avoids
    // the interpolation distortion that linearly-interpolated vertex
    // ray-directions would produce (apparent sun-size drift as the
    // camera rotates).
    vec2 ndc = (gl_FragCoord.xy / uViewport) * 2.0 - 1.0;
    vec4 nearH = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farH  = uInvViewProj * vec4(ndc,  1.0, 1.0);
    vec3 R = normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);

    // No sun disc — Beatrice's threshold-light has no body in the
    // sky. Only the scattering remains.
    vec3 col = atmosphere(uCamPos, R, uSunDir, uSunIntensity, 1e9);

    col = col * uExposure;
    col = col / (col + vec3(1.0));
    fragColor = vec4(col, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLuint sVao = 0;
GLuint sVbo = 0;
GLint sUniInvVPLoc = -1;
GLint sUniViewportLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;

} // namespace

bool initSkyPass()
{
    // Compose fragment shader: declarations + atmosphere() + main.
    const std::string fs = std::string(kSkyFSCore) + kAtmosphereGLSL + kSkyFSMain;
    sProgram = engine::gl::compileProgram(kSkyVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniInvVPLoc = glGetUniformLocation(sProgram, "uInvViewProj");
    sUniViewportLoc = glGetUniformLocation(sProgram, "uViewport");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");

    glGenVertexArrays(1, &sVao);
    glGenBuffers(1, &sVbo);
    glBindVertexArray(sVao);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return true;
}

void shutdownSkyPass()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    if (sVbo != 0)
    {
        glDeleteBuffers(1, &sVbo);
        sVbo = 0;
    }
    if (sVao != 0)
    {
        glDeleteVertexArrays(1, &sVao);
        sVao = 0;
    }
    sUniInvVPLoc = sUniViewportLoc = sUniSunDirLoc = sUniSunIntensityLoc = sUniCamPosLoc =
        sUniExposureLoc = -1;
}

void drawSky(const glm::mat4& inv_view_proj, const glm::vec3& sun_dir,
             const glm::vec3& sun_intensity, const glm::vec3& cam_pos, float exposure)
{
    glUseProgram(sProgram);

    const float len =
        std::sqrt(sun_dir.x * sun_dir.x + sun_dir.y * sun_dir.y + sun_dir.z * sun_dir.z);
    const float inv = (len > 1e-6f) ? (1.0f / len) : 1.0f;
    glUniform3f(sUniSunDirLoc, sun_dir.x * inv, sun_dir.y * inv, sun_dir.z * inv);
    glUniform3f(sUniSunIntensityLoc, sun_intensity.x, sun_intensity.y, sun_intensity.z);
    glUniform3f(sUniCamPosLoc, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform1f(sUniExposureLoc, exposure);
    glUniform2f(sUniViewportLoc, static_cast<float>(windowWidth()),
                static_cast<float>(windowHeight()));
    glUniformMatrix4fv(sUniInvVPLoc, 1, GL_FALSE, glm::value_ptr(inv_view_proj));

    GLboolean depth_test_was;
    GLboolean depth_mask_was;
    glGetBooleanv(GL_DEPTH_TEST, &depth_test_was);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindVertexArray(sVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    if (depth_test_was)
        glEnable(GL_DEPTH_TEST);
    glDepthMask(depth_mask_was);
}

} // namespace selva::render
