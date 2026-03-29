#include "systems/TileMapRenderer.h"

#include "gl/ShaderUtils.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Shaders -- textured tile rendering with color fallback.
//
// When a tileset is loaded, UV coordinates sample the tileset atlas.
// When no tileset is loaded, uUseTexture=0 and flat colors are used.
// ---------------------------------------------------------------------------

static const char* kVertSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProjection;

out vec2 vUV;
out vec4 vColor;

void main()
{
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)glsl";

static const char* kFragSrc = R"glsl(
#version 330 core
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTileset;
uniform int       uUseTexture;

out vec4 fragColor;

void main()
{
    if (uUseTexture != 0)
        fragColor = texture(uTileset, vUV);
    else
        fragColor = vColor;
}
)glsl";

// ---------------------------------------------------------------------------
// Static GL state
// ---------------------------------------------------------------------------

static GLuint sTMProgram = 0;
static GLuint sTMVao = 0;
static GLuint sTMVbo = 0;
static int sTMVertexCount = 0;
static uint32_t sTilesetTexId = 0;
static bool sHasTileset = false;

// Cached uniform locations — avoids glGetUniformLocation per frame.
static GLint sLocProjection = -1;
static GLint sLocUseTexture = -1;
static GLint sLocTileset = -1;

// Map dimensions for frustum culling (set in upload, read in render).
static int sMapWidth = 0;
static int sMapHeight = 0;
static float sTileSize = 0.0f;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TileMapRenderer::init()
{
    GLuint vert = engine::gl::compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint frag = engine::gl::compileShader(GL_FRAGMENT_SHADER, kFragSrc);

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

    sLocProjection = glGetUniformLocation(sTMProgram, "uProjection");
    sLocUseTexture = glGetUniformLocation(sTMProgram, "uUseTexture");
    sLocTileset = glGetUniformLocation(sTMProgram, "uTileset");

    // Vertex layout: vec2 pos + vec2 uv + vec4 color = 8 floats per vertex.
    glGenVertexArrays(1, &sTMVao);
    glGenBuffers(1, &sTMVbo);

    glBindVertexArray(sTMVao);
    glBindBuffer(GL_ARRAY_BUFFER, sTMVbo);

    const int stride = 8 * static_cast<int>(sizeof(float));
    // aPos -- location 0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    // aUV -- location 1
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    // aColor -- location 2
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void TileMapRenderer::upload(const TileMap& map, const TileConfig& config, TextureManager& tm)
{
    if (!map.valid())
        return;

    // Load tileset texture if configured.
    int atlasW = 0;
    int atlasH = 0;
    sHasTileset = false;

    if (!config.tileset_path.empty())
    {
        sTilesetTexId = tm.load(config.tileset_path);
        tm.getDimensions(config.tileset_path, atlasW, atlasH);
        sHasTileset = (atlasW > 0 && atlasH > 0);
    }

    // Build interleaved vertex data.
    // Each tile = 2 triangles = 6 vertices x 8 floats.
    const auto tile_count =
        static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    std::vector<float> verts;
    verts.reserve(tile_count * 48); // 6 verts * 8 floats

    const float ts = static_cast<float>(TileMap::TILE_SIZE);

    for (int row = 0; row < map.height; ++row)
    {
        for (int col = 0; col < map.width; ++col)
        {
            const auto& tile = map.at(col, row);

            // Look up visual (UV + fallback color) from config.
            float tr = 1.0f, tg = 1.0f, tb = 1.0f;
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

            auto vit = config.tile_visuals.find(tile.tile_id);
            if (vit != config.tile_visuals.end())
            {
                const auto& vis = vit->second;
                if (sHasTileset)
                {
                    const float tw = static_cast<float>(atlasW);
                    const float th = static_cast<float>(atlasH);
                    const float tileF = 32.0f;
                    const float c = static_cast<float>(vis.uv_col);
                    const float r = static_cast<float>(vis.uv_row);
                    u0 = c * tileF / tw;
                    v0 = r * tileF / th;
                    u1 = (c + 1.0f) * tileF / tw;
                    v1 = (r + 1.0f) * tileF / th;
                }
                else
                {
                    tr = vis.r;
                    tg = vis.g;
                    tb = vis.b;
                }
            }

            const float x0 = static_cast<float>(col) * ts;
            const float y0 = static_cast<float>(row) * ts;
            const float x1 = x0 + ts;
            const float y1 = y0 + ts;

            auto push = [&](float x, float y, float u, float v)
            {
                verts.push_back(x);
                verts.push_back(y);
                verts.push_back(u);
                verts.push_back(v);
                verts.push_back(tr);
                verts.push_back(tg);
                verts.push_back(tb);
                verts.push_back(1.0f);
            };

            push(x0, y0, u0, v0);
            push(x1, y0, u1, v0);
            push(x1, y1, u1, v1); // tri 1
            push(x0, y0, u0, v0);
            push(x1, y1, u1, v1);
            push(x0, y1, u0, v1); // tri 2
        }
    }

    sMapWidth = map.width;
    sMapHeight = map.height;
    sTileSize = ts;
    sTMVertexCount = static_cast<int>(verts.size() / 8);

    glBindVertexArray(sTMVao);
    glBindBuffer(GL_ARRAY_BUFFER, sTMVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void TileMapRenderer::clear()
{
    sTMVertexCount = 0;
    sMapWidth = 0;
    sMapHeight = 0;
    sTileSize = 0.0f;
}

void TileMapRenderer::render(float camX, float camY, int windowW, int windowH)
{
    ZoneScopedN("TileMapRenderer");
    if (sTMVertexCount == 0)
        return;

    const float half_w = static_cast<float>(windowW) * 0.5f;
    const float half_h = static_cast<float>(windowH) * 0.5f;

    const float snap_x = std::round(camX);
    const float snap_y = std::round(camY);

    float proj[16];
    engine::gl::buildOrtho(proj, snap_x - half_w, snap_x + half_w, snap_y + half_h,
                           snap_y - half_h);

    glUseProgram(sTMProgram);
    glUniformMatrix4fv(sLocProjection, 1, GL_FALSE, proj);
    glUniform1i(sLocUseTexture, sHasTileset ? 1 : 0);

    if (sHasTileset)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(sTilesetTexId));
        glUniform1i(sLocTileset, 0);
    }

    glBindVertexArray(sTMVao);

    // Frustum cull: draw only tiles visible on screen (row + column).
    // One draw call per visible row, each spanning only visible columns.
    if (sMapWidth > 0 && sMapHeight > 0 && sTileSize > 0.0f)
    {
        static constexpr int VERTS_PER_TILE = 6;
        int row0 = static_cast<int>((snap_y - half_h) / sTileSize) - 1;
        int row1 = static_cast<int>((snap_y + half_h) / sTileSize) + 1;
        int col0 = static_cast<int>((snap_x - half_w) / sTileSize) - 1;
        int col1 = static_cast<int>((snap_x + half_w) / sTileSize) + 1;
        row0 = std::max(row0, 0);
        row1 = std::min(row1, sMapHeight - 1);
        col0 = std::max(col0, 0);
        col1 = std::min(col1, sMapWidth - 1);

        const int colSpan = (col1 - col0 + 1) * VERTS_PER_TILE;
        for (int r = row0; r <= row1; ++r)
        {
            const int first = (r * sMapWidth + col0) * VERTS_PER_TILE;
            glDrawArrays(GL_TRIANGLES, first, colSpan);
        }
    }
    else
    {
        glDrawArrays(GL_TRIANGLES, 0, sTMVertexCount);
    }

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
    sTilesetTexId = 0;
    sHasTileset = false;
    sMapWidth = 0;
    sMapHeight = 0;
    sTileSize = 0.0f;
}
