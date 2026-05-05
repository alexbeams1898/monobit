#include "Engine.h"
#include "gl/ShaderUtils.h"

#include <cstdio>
#include <glad/glad.h>

// Vertex shader: takes one 2D position per vertex, writes it straight to
// gl_Position. Coordinates are in clip space ([-1, +1] on each axis) so the
// CPU side has to provide vertices already in that range; no model/view/
// projection transform yet.
static const char* kQuadVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

// Fragment shader: outputs a solid color, taken from the uColor uniform set
// by the CPU. Same color for every pixel in the draw call.
static const char* kQuadFragmentShader = R"glsl(
#version 330 core
out vec4 fragColor;

uniform vec4 uColor;

void main()
{
    fragColor = uColor;
}
)glsl";

// GPU shader program (vertex + fragment linked). 0 = not built / failed.
static GLuint sQuadProgram = 0;

// Vertex Array Object + Vertex Buffer Object holding the quad's geometry.
static GLuint sQuadVao = 0;
static GLuint sQuadVbo = 0;

// Cached uniform location for setting the quad's color each frame.
static GLint sQuadColorLoc = -1;

// Build the GL state needed to draw the quad: upload vertex data, declare its
// layout, resolve the uColor uniform location. Runs once after the GL context
// exists. The quad is centered at clip-space origin and spans from -0.25 to
// +0.25 on each axis (a 25% x 25% square in the middle of the window).
static void initQuad()
{
    // Two triangles forming a square, six vertices, two floats each (x, y).
    // Triangle strip would also work; using GL_TRIANGLES keeps the data shape
    // explicit since we're learning.
    static constexpr float kVertices[] = {
        // first triangle (bottom-left, bottom-right, top-right)
        -0.25f, -0.25f,
         0.25f, -0.25f,
         0.25f,  0.25f,
        // second triangle (bottom-left, top-right, top-left)
        -0.25f, -0.25f,
         0.25f,  0.25f,
        -0.25f,  0.25f,
    };

    glGenVertexArrays(1, &sQuadVao);
    glGenBuffers(1, &sQuadVbo);

    glBindVertexArray(sQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, sQuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);

    // Vertex layout: location 0 (matches `layout(location = 0)` in the
    // vertex shader) is two floats, tightly packed (stride = 2 floats).
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    sQuadColorLoc = glGetUniformLocation(sQuadProgram, "uColor");
}

// Issue the draw call. Bind the program, set the color uniform, bind the
// VAO, draw 6 vertices = 2 triangles = one square.
static void drawQuad()
{
    glUseProgram(sQuadProgram);
    glUniform4f(sQuadColorLoc, 1.0f, 0.2f, 0.2f, 1.0f); // solid red
    glBindVertexArray(sQuadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

// Engine renderUI callback: fires every frame after the engine clears + does
// its world-render pass, before SwapWindow. Selva Oscura uses this slot for
// its own GL drawing for now.
static void selvaRenderUI(Engine& /*engine*/, EntityManager& /*em*/)
{
    drawQuad();
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

    sQuadProgram = engine::gl::compileProgram(kQuadVertexShader, kQuadFragmentShader);
    if (sQuadProgram == 0)
    {
        std::fprintf(stderr, "Quad shader compile/link failed\n");
        return 1;
    }
    initQuad();

    engine.setRenderUI(&selvaRenderUI);

    engine.run();
    engine.shutdown();
    return 0;
}
