#include "systems/TileMapRenderer.h"

#include "gl/ShaderUtils.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <glad/glad.h>

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

// Two SEPARATE sparse meshes (only cells with a prop), each its own VAO/VBO, empty
// by default (zero verts -> no-op). DECORATION draws BEFORE characters (flowers the
// player walks on); OVERHANG draws AFTER (canopy tops the player walks behind).
static GLuint sDecVao = 0;
static GLuint sDecVbo = 0;
static int sDecVertexCount = 0;
static GLuint sOverVao = 0;
static GLuint sOverVbo = 0;
static int sOverVertexCount = 0;

// Cached uniform locations — avoids glGetUniformLocation per frame.
static GLint sLocProjection = -1;
static GLint sLocUseTexture = -1;
static GLint sLocTileset = -1;

// Map dimensions for frustum culling (set in upload, read in render).
static int sMapWidth = 0;
static int sMapHeight = 0;
static float sTileSize = 0.0f;

// Emit one tile's quad (6 verts x 8 floats) into `verts`. Shared by the ground
// and overhang bakes so both layers rasterize identically.
static void emitTileQuad(std::vector<float>& verts, const TileConfig& config, int tileId, int col,
                         int row, float ts, bool hasTileset, int atlasW, int atlasH)
{
    // A tile id with NO registered visual is EMPTY SPACE: emit a degenerate (zero-area)
    // quad so the dense mesh keeps its cell indexing (render() culls by index math) while
    // rasterizing nothing -- the clear color shows through. This is how a map says
    // "nothing here" without a dedicated blank tile in every atlas.
    const auto vit = config.tile_visuals.find(tileId);
    if (vit == config.tile_visuals.end())
    {
        for (int i = 0; i < 6; ++i)
            verts.insert(verts.end(), {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f});
        return;
    }
    const auto& vis = vit->second;
    float tr = 1.0f, tg = 1.0f, tb = 1.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (hasTileset)
    {
        const float tw = static_cast<float>(atlasW);
        const float th = static_cast<float>(atlasH);
        const float tileF = static_cast<float>(config.atlas_tile_size);
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
    const float x0 = static_cast<float>(col) * ts;
    const float y0 = static_cast<float>(row) * ts;
    const float x1 = x0 + ts;
    const float y1 = y0 + ts;
    const auto push = [&](float x, float y, float u, float v)
    { verts.insert(verts.end(), {x, y, u, v, tr, tg, tb, 1.0f}); };
    push(x0, y0, u0, v0);
    push(x1, y0, u1, v0);
    push(x1, y1, u1, v1); // tri 1
    push(x0, y0, u0, v0);
    push(x1, y1, u1, v1);
    push(x0, y1, u0, v1); // tri 2
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TileMapRenderer::init()
{
    const GLuint vert = engine::gl::compileShader(GL_VERTEX_SHADER, kVertSrc);
    const GLuint frag = engine::gl::compileShader(GL_FRAGMENT_SHADER, kFragSrc);

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
    // Ground + overhang layers share the same layout; set up both VAO/VBO pairs.
    const auto setupVao = [](GLuint& vao, GLuint& vbo)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        const int stride = 8 * static_cast<int>(sizeof(float));
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0)); // aPos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(2 * sizeof(float))); // aUV
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(4 * sizeof(float))); // aColor
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    };
    setupVao(sTMVao, sTMVbo);
    setupVao(sDecVao, sDecVbo);
    setupVao(sOverVao, sOverVbo);
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
    const float ts = static_cast<float>(map.tile_size);

    // GROUND layer: a DENSE mesh (every cell), so render() can frustum-cull by
    // row/column span (index = row*width+col).
    std::vector<float> verts;
    verts.reserve(tile_count * 48); // 6 verts * 8 floats
    for (int row = 0; row < map.height; ++row)
        for (int col = 0; col < map.width; ++col)
            emitTileQuad(verts, config, map.at(col, row).tile_id, col, row, ts, sHasTileset, atlasW,
                         atlasH);

    sMapWidth = map.width;
    sMapHeight = map.height;
    sTileSize = ts;
    sTMVertexCount = static_cast<int>(verts.size() / 8);

    glBindVertexArray(sTMVao);
    glBindBuffer(GL_ARRAY_BUFFER, sTMVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    // Bake a SPARSE layer (decoration / overhang): only cells with a prop
    // (tile_id != 0) get a quad; drawn whole (prop count is small, no cull). Returns
    // the vertex count and uploads into `vbo`.
    const auto bakeSparse = [&](const std::vector<TileMap::Tile>& layer, GLuint vbo) -> int
    {
        std::vector<float> sv;
        if (layer.size() == map.tiles.size())
            for (int row = 0; row < map.height; ++row)
                for (int col = 0; col < map.width; ++col)
                {
                    const int id =
                        layer[static_cast<std::size_t>(row) * static_cast<std::size_t>(map.width) +
                              static_cast<std::size_t>(col)]
                            .tile_id;
                    if (id != 0)
                        emitTileQuad(sv, config, id, col, row, ts, sHasTileset, atlasW, atlasH);
                }
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sv.size() * sizeof(float)), sv.data(),
                     GL_STATIC_DRAW);
        return static_cast<int>(sv.size() / 8);
    };
    sDecVertexCount = bakeSparse(map.decoration, sDecVbo);
    sOverVertexCount = bakeSparse(map.overhang, sOverVbo);
    glBindVertexArray(0);
}

void TileMapRenderer::clear()
{
    sTMVertexCount = 0;
    sDecVertexCount = 0;
    sOverVertexCount = 0;
    sMapWidth = 0;
    sMapHeight = 0;
    sTileSize = 0.0f;
}

namespace
{
// Set the projection + shader + tileset for a tilemap draw. Shared by the ground
// and overhang passes (same camera, program, atlas). Returns the snapped camera +
// half-extents so the caller can frustum-cull.
struct DrawView
{
    float snap_x, snap_y, half_w, half_h;
};
DrawView beginTileDraw(float camX, float camY, int windowW, int windowH, float zoom)
{
    DrawView v;
    v.half_w = static_cast<float>(windowW) * 0.5f / zoom;
    v.half_h = static_cast<float>(windowH) * 0.5f / zoom;
    v.snap_x = std::round(camX);
    v.snap_y = std::round(camY);
    float proj[16];
    engine::gl::buildOrtho(proj, v.snap_x - v.half_w, v.snap_x + v.half_w, v.snap_y + v.half_h,
                           v.snap_y - v.half_h);
    glUseProgram(sTMProgram);
    glUniformMatrix4fv(sLocProjection, 1, GL_FALSE, proj);
    glUniform1i(sLocUseTexture, sHasTileset ? 1 : 0);
    if (sHasTileset)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(sTilesetTexId));
        glUniform1i(sLocTileset, 0);
    }
    return v;
}
} // namespace

void TileMapRenderer::render(float camX, float camY, int windowW, int windowH, float zoom)
{
    ZoneScopedN("TileMapRenderer");
    if (sTMVertexCount == 0)
        return;

    const DrawView v = beginTileDraw(camX, camY, windowW, windowH, zoom);
    glBindVertexArray(sTMVao);

    // Frustum cull the DENSE ground mesh: one draw call per visible row, each
    // spanning only visible columns (index = row*width+col).
    if (sMapWidth > 0 && sMapHeight > 0 && sTileSize > 0.0f)
    {
        static constexpr int VERTS_PER_TILE = 6;
        int row0 = static_cast<int>((v.snap_y - v.half_h) / sTileSize) - 1;
        int row1 = static_cast<int>((v.snap_y + v.half_h) / sTileSize) + 1;
        int col0 = static_cast<int>((v.snap_x - v.half_w) / sTileSize) - 1;
        int col1 = static_cast<int>((v.snap_x + v.half_w) / sTileSize) + 1;
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

namespace
{
// Draw one sparse layer mesh whole (prop count is small; no per-row cull).
void drawSparse(GLuint vao, int vertexCount, float camX, float camY, int windowW, int windowH,
                float zoom)
{
    if (vertexCount == 0)
        return; // empty layer -> no-op (games without it are unaffected)
    beginTileDraw(camX, camY, windowW, windowH, zoom);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}
} // namespace

void TileMapRenderer::renderDecoration(float camX, float camY, int windowW, int windowH, float zoom)
{
    ZoneScopedN("TileMapRenderer::decoration");
    drawSparse(sDecVao, sDecVertexCount, camX, camY, windowW, windowH, zoom);
}

void TileMapRenderer::renderOverhang(float camX, float camY, int windowW, int windowH, float zoom)
{
    ZoneScopedN("TileMapRenderer::overhang");
    drawSparse(sOverVao, sOverVertexCount, camX, camY, windowW, windowH, zoom);
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
    if (sDecVbo)
    {
        glDeleteBuffers(1, &sDecVbo);
        sDecVbo = 0;
    }
    if (sDecVao)
    {
        glDeleteVertexArrays(1, &sDecVao);
        sDecVao = 0;
    }
    if (sOverVbo)
    {
        glDeleteBuffers(1, &sOverVbo);
        sOverVbo = 0;
    }
    if (sOverVao)
    {
        glDeleteVertexArrays(1, &sOverVao);
        sOverVao = 0;
    }
    if (sTMProgram)
    {
        glDeleteProgram(sTMProgram);
        sTMProgram = 0;
    }
    sTMVertexCount = 0;
    sDecVertexCount = 0;
    sOverVertexCount = 0;
    sTilesetTexId = 0;
    sHasTileset = false;
    sMapWidth = 0;
    sMapHeight = 0;
    sTileSize = 0.0f;
}
