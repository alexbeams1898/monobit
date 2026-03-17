#include "systems/TileMapRenderer.h"

#include <cmath>
#include <glad/glad.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Shaders — simplified for static geometry.
// World-space position and color are baked into the VBO at upload time,
// so only uProjection is needed per frame (no per-tile uniforms).
// ---------------------------------------------------------------------------

static const char* kVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uProjection;

out vec4 vColor;

void main()
{
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 330 core
in vec4 vColor;
out vec4 fragColor;

void main()
{
    fragColor = vColor;
}
)glsl";

// ---------------------------------------------------------------------------
// Static GL state
// ---------------------------------------------------------------------------

static GLuint sTMProgram = 0;
static GLuint sTMVao = 0;
static GLuint sTMVbo = 0;
static int sTMVertexCount = 0; // set by upload(), read by render()

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static GLuint compileTMShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[TileMapRenderer] Shader compile error:\n" << log << "\n";
    }
    return shader;
}

// Column-major orthographic projection (same convention as RenderSystem).
static void buildTMOrtho(float mat[16], float left, float right, float bottom, float top)
{
    const float rml = right - left;
    const float tmb = top - bottom;
    // clang-format off
    mat[ 0] = 2.0f / rml;  mat[ 4] = 0.0f;        mat[ 8] = 0.0f;   mat[12] = -(right + left)   / rml;
    mat[ 1] = 0.0f;        mat[ 5] = 2.0f / tmb;  mat[ 9] = 0.0f;   mat[13] = -(top   + bottom) / tmb;
    mat[ 2] = 0.0f;        mat[ 6] = 0.0f;         mat[10] = -1.0f;  mat[14] = 0.0f;
    mat[ 3] = 0.0f;        mat[ 7] = 0.0f;         mat[11] = 0.0f;   mat[15] = 1.0f;
    // clang-format on
}

// Return tint color for a tile type when no sprite is present.
static void tileTypeTint(TileType type, float& r, float& g, float& b)
{
    switch (type)
    {
    case TileType::Floor:
        r = 0.20f;
        g = 0.20f;
        b = 0.20f;
        break;
    case TileType::Wall:
        r = 0.15f;
        g = 0.12f;
        b = 0.10f;
        break;
    case TileType::DoorFrame:
        r = 0.35f;
        g = 0.30f;
        b = 0.25f;
        break;
    case TileType::Obstacle:
        r = 0.25f;
        g = 0.18f;
        b = 0.12f;
        break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TileMapRenderer::init()
{
    GLuint vert = compileTMShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint frag = compileTMShader(GL_FRAGMENT_SHADER, kFragSrc);

    sTMProgram = glCreateProgram();
    glAttachShader(sTMProgram, vert);
    glAttachShader(sTMProgram, frag);
    glLinkProgram(sTMProgram);

    GLint ok = 0;
    glGetProgramiv(sTMProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(sTMProgram, sizeof(log), nullptr, log);
        std::cerr << "[TileMapRenderer] Program link error:\n" << log << "\n";
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    // Create VAO/VBO with the new vertex layout: vec2 pos + vec4 color (24 bytes/vertex).
    // No data yet — upload() fills this after map generation.
    glGenVertexArrays(1, &sTMVao);
    glGenBuffers(1, &sTMVbo);

    glBindVertexArray(sTMVao);
    glBindBuffer(GL_ARRAY_BUFFER, sTMVbo);

    // aPos  — location 0, 2 floats, offset 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // aColor — location 1, 4 floats, offset 8 bytes (after aPos)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void TileMapRenderer::upload(const TileMap& map)
{
    if (!map.valid())
        return;

    // Build interleaved vertex data for all tiles.
    // Each tile = 2 triangles = 6 vertices × 6 floats (x, y, r, g, b, a).
    const std::size_t tile_count =
        static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    std::vector<float> verts;
    verts.reserve(tile_count * 36); // 6 verts * 6 floats

    const float ts = static_cast<float>(TileMap::TILE_SIZE);

    for (int row = 0; row < map.height; ++row)
    {
        for (int col = 0; col < map.width; ++col)
        {
            const auto& tile = map.at(col, row);

            float tr = 0.0f, tg = 0.0f, tb = 0.0f;
            tileTypeTint(tile.type, tr, tg, tb);

            const float x0 = static_cast<float>(col) * ts;
            const float y0 = static_cast<float>(row) * ts;
            const float x1 = x0 + ts;
            const float y1 = y0 + ts;

            // Two triangles (CCW), 6 vertices.
            auto push = [&](float x, float y)
            {
                verts.push_back(x);
                verts.push_back(y);
                verts.push_back(tr);
                verts.push_back(tg);
                verts.push_back(tb);
                verts.push_back(1.0f);
            };

            push(x0, y0);
            push(x1, y0);
            push(x1, y1); // triangle 1
            push(x0, y0);
            push(x1, y1);
            push(x0, y1); // triangle 2
        }
    }

    sTMVertexCount = static_cast<int>(verts.size() / 6);

    glBindVertexArray(sTMVao);
    glBindBuffer(GL_ARRAY_BUFFER, sTMVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void TileMapRenderer::render(float camX, float camY, int windowW, int windowH)
{
    ZoneScopedN("TileMapRenderer");
    if (sTMVertexCount == 0)
        return;

    const float half_w = static_cast<float>(windowW) * 0.5f;
    const float half_h = static_cast<float>(windowH) * 0.5f;

    // Camera snapped to integer pixels — prevents 1-pixel gaps between tiles.
    const float snap_x = std::round(camX);
    const float snap_y = std::round(camY);

    // Orthographic projection centered on camera (Y-down: bottom > top in call).
    float proj[16];
    buildTMOrtho(proj, snap_x - half_w, snap_x + half_w, snap_y + half_h, snap_y - half_h);

    glUseProgram(sTMProgram);
    glUniformMatrix4fv(glGetUniformLocation(sTMProgram, "uProjection"), 1, GL_FALSE, proj);
    glBindVertexArray(sTMVao);
    glDrawArrays(GL_TRIANGLES, 0, sTMVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}

void TileMapRenderer::shutdown()
{
    if (sTMVbo)
    {
        glDeleteBuffers(1, &sTMVbo);
        sTMVbo = 0;
    }
    if (sTMVao)
    {
        glDeleteVertexArrays(1, &sTMVao);
        sTMVao = 0;
    }
    if (sTMProgram)
    {
        glDeleteProgram(sTMProgram);
        sTMProgram = 0;
    }
    sTMVertexCount = 0;
}
