#include "Engine.h"
#include "gl/ShaderUtils.h"

#include <chrono>
#include <cstdio>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Vertex shader: takes a 3D position + per-vertex color. MVP transforms the
// position; color is passed through to the fragment shader (the GPU
// interpolates it across the triangle for free).
static const char* kCubeVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMVP;

out vec3 vColor;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

// Fragment shader: outputs the interpolated color from the vertex shader.
static const char* kCubeFragmentShader = R"glsl(
#version 330 core
in vec3 vColor;
out vec4 fragColor;

void main()
{
    fragColor = vec4(vColor, 1.0);
}
)glsl";

// GPU shader program (vertex + fragment linked). 0 = not built / failed.
static GLuint sCubeProgram = 0;

// Vertex Array Object + Vertex Buffer Object + Element Buffer Object holding
// the cube's geometry. EBO = index buffer.
static GLuint sCubeVao = 0;
static GLuint sCubeVbo = 0;
static GLuint sCubeEbo = 0;

// Cached uniform locations. Resolved once at init.
static GLint sCubeMvpLoc = -1;

// Window size (read once at init for the projection's aspect ratio).
static int sWindowW = 0;
static int sWindowH = 0;

// Build the GL state needed to draw the cube.
//
// 8 unique vertices (one per cube corner), each with position + color. The
// index buffer says "draw 36 indices = 12 triangles = 6 faces" using those 8
// vertices. Per-corner color means each face is a smooth gradient between
// its 4 corners, which makes the cube's 3D shape obvious as it rotates.
static void initCube()
{
    // 8 corners of a cube centered at origin, half-extent 0.5. Each vertex:
    //   x, y, z,    r, g, b
    // Colors picked so opposite corners are roughly complementary -- gives
    // each face a distinct gradient.
    // clang-format off
    static constexpr float kVertices[] = {
        // back face corners (z = -0.5)
        -0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f, // 0: red
         0.5f, -0.5f, -0.5f,   1.0f, 1.0f, 0.0f, // 1: yellow
         0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f, // 2: green
        -0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 1.0f, // 3: cyan
        // front face corners (z = +0.5)
        -0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 1.0f, // 4: magenta
         0.5f, -0.5f,  0.5f,   1.0f, 1.0f, 1.0f, // 5: white
         0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f, // 6: blue
        -0.5f,  0.5f,  0.5f,   0.5f, 0.5f, 0.5f, // 7: grey
    };

    // 36 indices = 12 triangles = 6 faces. Each face is two triangles sharing
    // a diagonal. Winding order (counter-clockwise as seen from outside the
    // cube) is consistent so back-face culling could be enabled later.
    static constexpr unsigned int kIndices[] = {
        // back face (looking down -Z)
        0, 2, 1,   0, 3, 2,
        // front face (looking down +Z)
        4, 5, 6,   4, 6, 7,
        // left face
        0, 4, 7,   0, 7, 3,
        // right face
        1, 2, 6,   1, 6, 5,
        // bottom face
        0, 1, 5,   0, 5, 4,
        // top face
        3, 7, 6,   3, 6, 2,
    };
    // clang-format on

    glGenVertexArrays(1, &sCubeVao);
    glGenBuffers(1, &sCubeVbo);
    glGenBuffers(1, &sCubeEbo);

    glBindVertexArray(sCubeVao);

    glBindBuffer(GL_ARRAY_BUFFER, sCubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sCubeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    // Vertex layout: 6 floats per vertex (3 position + 3 color), interleaved.
    // location 0 = position (3 floats, offset 0).
    // location 1 = color    (3 floats, offset 3*sizeof(float)).
    constexpr int stride = 6 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    sCubeMvpLoc = glGetUniformLocation(sCubeProgram, "uMVP");
}

// Issue the draw call. Bind the program, set the MVP uniform, bind the VAO,
// drawElements pulls vertex indices from the EBO.
static void drawCube()
{
    // Model: rotate around an axis based on wall-clock time. Spinning around
    // a non-cardinal axis gives the cube a tumbling motion that shows off
    // multiple faces simultaneously.
    static const auto sStartTime = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - sStartTime).count();
    const float angle = seconds * (glm::two_pi<float>() / 4.0f);
    const glm::mat4 model =
        glm::rotate(glm::mat4(1.0f), angle, glm::normalize(glm::vec3(0.6f, 1.0f, 0.3f)));

    // View: camera at (0, 0, 3) looking at the origin.
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect =
        sWindowH > 0 ? static_cast<float>(sWindowW) / static_cast<float>(sWindowH) : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);

    const glm::mat4 mvp = proj * view * model;

    glUseProgram(sCubeProgram);
    glUniformMatrix4fv(sCubeMvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
    glBindVertexArray(sCubeVao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
}

// Free the cube's GL resources. Must run while the GL context is still alive
// (i.e. before Engine::shutdown destroys it). Symmetric to initCube.
static void shutdownCube()
{
    glDeleteBuffers(1, &sCubeEbo);
    glDeleteBuffers(1, &sCubeVbo);
    glDeleteVertexArrays(1, &sCubeVao);
    glDeleteProgram(sCubeProgram);
    sCubeEbo = 0;
    sCubeVbo = 0;
    sCubeVao = 0;
    sCubeProgram = 0;
}

// Engine renderWorld callback: fires every frame between framebuffer clear
// and the UI pass. This is where Selva Oscura's 3D rendering lives. Will
// eventually be three passes (geometry to FBO, dither/threshold/outline
// post-process, blit) but for now it's just the test cube.
static void selvaRenderWorld(Engine& /*engine*/, EntityManager& /*em*/, float /*camX*/,
                             float /*camY*/, float /*alpha*/)
{
    drawCube();
}

int main(int /*argc*/, char* /*argv*/[])
{
    Engine engine;

    if (!engine.init("Selva Oscura", 1280, 720))
    {
        std::fprintf(stderr, "Engine init failed\n");
        return 1;
    }

    engine.setClearColor(0.0f, 0.0f, 0.0f);

    sCubeProgram = engine::gl::compileProgram(kCubeVertexShader, kCubeFragmentShader);
    if (sCubeProgram == 0)
    {
        std::fprintf(stderr, "Cube shader compile/link failed\n");
        return 1;
    }
    sWindowW = engine.windowWidth();
    sWindowH = engine.windowHeight();
    initCube();

    engine.setRenderWorld(&selvaRenderWorld);

    engine.run();
    shutdownCube();
    engine.shutdown();
    return 0;
}
