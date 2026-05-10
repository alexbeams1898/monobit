#include "render/SceneShaders.h"

#include "gl/ShaderUtils.h"

#include <glm/gtc/type_ptr.hpp>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Vertex shader: 3D position + per-vertex grayscale shade. uModel transforms
// the vertex to world space; uViewProj projects world to clip space. Splitting
// model from view-projection lets one shader draw many objects per frame —
// each draw call updates uModel, uViewProj is set once per frame.
const char* kSceneVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aShade;

uniform mat4 uModel;
uniform mat4 uViewProj;

out float vShade;

void main()
{
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
    vShade = aShade;
}
)glsl";

// Fragment shader: outputs the interpolated grayscale shade with full alpha.
// Multiplied by uTint so the same geometry can render in different shades
// (scene cube vs player cube) without duplicating vertex buffers.
const char* kSceneFragmentShader = R"glsl(
#version 330 core
in float vShade;
out vec4 fragColor;

uniform float uTint;

void main()
{
    float g = clamp(vShade * uTint, 0.0, 1.0);
    fragColor = vec4(g, g, g, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLint sUniModelLoc = -1;
GLint sUniViewProjLoc = -1;
GLint sUniTintLoc = -1;

} // namespace

bool initSceneProgram()
{
    sProgram = engine::gl::compileProgram(kSceneVertexShader, kSceneFragmentShader);
    if (sProgram == 0)
        return false;
    sUniModelLoc = glGetUniformLocation(sProgram, "uModel");
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniTintLoc = glGetUniformLocation(sProgram, "uTint");
    return true;
}

void shutdownSceneProgram()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniModelLoc = -1;
    sUniViewProjLoc = -1;
    sUniTintLoc = -1;
}

void useSceneProgram()
{
    glUseProgram(sProgram);
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

} // namespace selva::render
