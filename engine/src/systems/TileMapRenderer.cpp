#include "systems/TileMapRenderer.h"

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

// Flat-color fallback when no tileset is configured.
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

    // Pre-compute UV rects for each tile type.
    struct UVRect
    {
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
    };

    auto makeUV = [&](const TileConfig::TileUV& tuv) -> UVRect
    {
        if (!sHasTileset)
            return {0.0f, 0.0f, 1.0f, 1.0f};
        const float tw = static_cast<float>(atlasW);
        const float th = static_cast<float>(atlasH);
        const float tileF = 32.0f;
        const float c = static_cast<float>(tuv.col);
        const float r = static_cast<float>(tuv.row);
        return {c * tileF / tw, r * tileF / th, (c + 1.0f) * tileF / tw, (r + 1.0f) * tileF / th};
    };

    UVRect floorUV = makeUV(config.floor_uv);
    UVRect wallUV = makeUV(config.wall_uv);
    UVRect doorUV = makeUV(config.door_uv);
    UVRect obstacleUV = makeUV(config.obstacle_uv);

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

            float tr = 1.0f, tg = 1.0f, tb = 1.0f;
            if (!sHasTileset)
                tileTypeTint(tile.type, tr, tg, tb);

            UVRect uv;
            switch (tile.type)
            {
            case TileType::Floor:
                uv = floorUV;
                break;
            case TileType::Wall:
                uv = wallUV;
                break;
            case TileType::DoorFrame:
                uv = doorUV;
                break;
            case TileType::Obstacle:
                uv = obstacleUV;
                break;
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

            push(x0, y0, uv.u0, uv.v0);
            push(x1, y0, uv.u1, uv.v0);
            push(x1, y1, uv.u1, uv.v1); // tri 1
            push(x0, y0, uv.u0, uv.v0);
            push(x1, y1, uv.u1, uv.v1);
            push(x0, y1, uv.u0, uv.v1); // tri 2
        }
    }

    sTMVertexCount = static_cast<int>(verts.size() / 8);

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

    const float snap_x = std::round(camX);
    const float snap_y = std::round(camY);

    float proj[16];
    buildTMOrtho(proj, snap_x - half_w, snap_x + half_w, snap_y + half_h, snap_y - half_h);

    glUseProgram(sTMProgram);
    glUniformMatrix4fv(glGetUniformLocation(sTMProgram, "uProjection"), 1, GL_FALSE, proj);
    glUniform1i(glGetUniformLocation(sTMProgram, "uUseTexture"), sHasTileset ? 1 : 0);

    if (sHasTileset)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(sTilesetTexId));
        glUniform1i(glGetUniformLocation(sTMProgram, "uTileset"), 0);
    }

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
    sTilesetTexId = 0;
    sHasTileset = false;
}
