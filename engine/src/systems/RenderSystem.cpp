#include "systems/RenderSystem.h"

#include "ecs/Components.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <iostream>
#include <vector>

// ---------------------------------------------------------------------------
// Shaders (inline — no asset loading required for engine-owned shaders).
//
// Vertex shader:
//   - Receives a unit quad [0,1]x[0,1] in local space.
//   - uModel scales and translates it to world-space pixel coordinates.
//   - uProjection converts pixel coordinates to OpenGL's NDC (-1..1) space
//     with (0,0) at the top-left corner of the window.
//
// Fragment shader:
//   - Samples the bound texture atlas at the UV computed from uSrcRect.
//   - uSrcRect = (x, y, w, h) in 0-1 UV space, describing which region of
//     the atlas to use for this sprite.
// ---------------------------------------------------------------------------

static const char* kVertexShaderSrc = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform mat4 uProjection;
uniform mat4 uModel;

out vec2 vUV;

void main()
{
    gl_Position = uProjection * uModel * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)glsl";

static const char* kFragmentShaderSrc = R"glsl(
#version 330 core
in vec2 vUV;

uniform sampler2D uTexture;
uniform vec4      uSrcRect; // (x, y, w, h) in 0-1 UV space

out vec4 fragColor;

void main()
{
    vec2 uv = uSrcRect.xy + vUV * uSrcRect.zw;
    fragColor = texture(uTexture, uv);
}
)glsl";

// ---------------------------------------------------------------------------
// Static state — kept in this translation unit; accessed only through the
// public static methods. Fine for a single-window game.
// ---------------------------------------------------------------------------

static GLuint sProgram = 0;
static GLuint sVAO = 0;
static GLuint sVBO = 0;
static int sWindowW = 0;
static int sWindowH = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static GLuint compileShader(GLenum type, const char* src)
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
        std::cerr << "[RenderSystem] Shader compile error:\n" << log << "\n";
    }
    return shader;
}

// Build a column-major 4x4 orthographic projection matrix.
// Maps pixel coordinates (origin top-left) to OpenGL NDC (-1..1).
// The camera offset shifts the world so the camera tracks the player.
static void buildOrtho(float mat[16], float left, float right, float bottom, float top)
{
    const float rml = right - left; // right minus left
    const float tmb = top - bottom; // top minus bottom

    // clang-format off
    mat[ 0] = 2.0f / rml;  mat[ 4] = 0.0f;         mat[ 8] = 0.0f;  mat[12] = -(right + left) / rml;
    mat[ 1] = 0.0f;        mat[ 5] = 2.0f / tmb;   mat[ 9] = 0.0f;  mat[13] = -(top + bottom) / tmb;
    mat[ 2] = 0.0f;        mat[ 6] = 0.0f;          mat[10] = -1.0f; mat[14] = 0.0f;
    mat[ 3] = 0.0f;        mat[ 7] = 0.0f;          mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

// Build a column-major 4x4 model matrix: translate to (x,y), then scale to (w,h).
static void buildModel(float mat[16], float x, float y, float w, float h)
{
    // clang-format off
    mat[ 0] = w;     mat[ 4] = 0.0f;  mat[ 8] = 0.0f;  mat[12] = x;
    mat[ 1] = 0.0f;  mat[ 5] = h;     mat[ 9] = 0.0f;  mat[13] = y;
    mat[ 2] = 0.0f;  mat[ 6] = 0.0f;  mat[10] = 1.0f;  mat[14] = 0.0f;
    mat[ 3] = 0.0f;  mat[ 7] = 0.0f;  mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void RenderSystem::init(int windowW, int windowH)
{
    sWindowW = windowW;
    sWindowH = windowH;

    // Compile and link shaders.
    GLuint vert = compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);

    sProgram = glCreateProgram();
    glAttachShader(sProgram, vert);
    glAttachShader(sProgram, frag);
    glLinkProgram(sProgram);

    GLint ok = 0;
    glGetProgramiv(sProgram, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(sProgram, sizeof(log), nullptr, log);
        std::cerr << "[RenderSystem] Program link error:\n" << log << "\n";
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    // Unit quad: two triangles covering [0,1]x[0,1], UV matches position.
    // Layout: x, y, u, v  (2 floats position + 2 floats UV per vertex).
    // clang-format off
    const float vertices[] = {
        // pos       uv
        0.0f, 0.0f,  0.0f, 0.0f, // top-left
        1.0f, 0.0f,  1.0f, 0.0f, // top-right
        1.0f, 1.0f,  1.0f, 1.0f, // bottom-right
        0.0f, 1.0f,  0.0f, 1.0f, // bottom-left
    };
    // clang-format on

    glGenVertexArrays(1, &sVAO);
    glGenBuffers(1, &sVBO);

    glBindVertexArray(sVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Attribute 0: position (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // Attribute 1: UV (vec2)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    // Enable alpha blending so transparent PNG regions are invisible.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void RenderSystem::render(EntityManager& em, TextureManager& tm, float camX, float camY)
{
    // Collect all renderable entities into a vector so we can sort by layer.
    struct DrawEntry
    {
        float x, y;
        int srcX, srcY, srcW, srcH;
        int layer;
        uint32_t texId;
        int texW, texH; // full texture dimensions for UV normalisation
    };

    std::vector<DrawEntry> drawList;
    drawList.reserve(64);

    for (auto [entity, transform, sprite] : em.registry().view<Transform, Sprite>().each())
    {
        if (sprite.texturePath.empty() && sprite.textureId == 0)
            continue;

        uint32_t texId = tm.load(sprite.texturePath);

        // Query texture dimensions so we can convert pixel src rects to 0-1 UV.
        glBindTexture(GL_TEXTURE_2D, texId);
        GLint texW = 0;
        GLint texH = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texH);
        glBindTexture(GL_TEXTURE_2D, 0);

        drawList.push_back({transform.x - static_cast<float>(sprite.srcW) * 0.5f, // top-left corner
                            transform.y - static_cast<float>(sprite.srcH) * 0.5f, sprite.srcX,
                            sprite.srcY, sprite.srcW, sprite.srcH, sprite.layer, texId, texW,
                            texH});
    }

    // Sort ascending by layer — lower layers drawn first (appear behind).
    std::sort(drawList.begin(), drawList.end(),
              [](const DrawEntry& a, const DrawEntry& b) { return a.layer < b.layer; });

    // Build orthographic projection centred on the camera position.
    // The camera sits at the centre of the window; the world scrolls around it.
    //
    // Round to the nearest integer pixel before building the projection.
    // Without this, sub-pixel camera positions (e.g. camX=641.67 at 200px/s,
    // dt=1/60) give each tile a slightly different fractional screen offset.
    // OpenGL's rasterizer then places adjacent tiles at different sub-pixel
    // boundaries → 1-pixel gaps appear on the north/west (leading) edges of
    // tiles whenever you move.  Integer snapping keeps all tiles on the same
    // pixel grid every frame.
    const float snapCamX = std::round(camX);
    const float snapCamY = std::round(camY);
    const float halfW = static_cast<float>(sWindowW) * 0.5f;
    const float halfH = static_cast<float>(sWindowH) * 0.5f;
    float proj[16];
    buildOrtho(proj, snapCamX - halfW, snapCamX + halfW, snapCamY + halfH, snapCamY - halfH);

    glUseProgram(sProgram);
    glUniformMatrix4fv(glGetUniformLocation(sProgram, "uProjection"), 1, GL_FALSE, proj);
    glUniform1i(glGetUniformLocation(sProgram, "uTexture"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(sVAO);

    for (const auto& e : drawList)
    {
        glBindTexture(GL_TEXTURE_2D, e.texId);

        // Model matrix: position at (e.x, e.y), scale to (srcW, srcH) pixels.
        float model[16];
        buildModel(model, e.x, e.y, static_cast<float>(e.srcW), static_cast<float>(e.srcH));
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, model);

        // Convert pixel src rect to 0-1 UV space for the atlas sample.
        const float tw = static_cast<float>(e.texW > 0 ? e.texW : 1);
        const float th = static_cast<float>(e.texH > 0 ? e.texH : 1);
        glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), static_cast<float>(e.srcX) / tw,
                    static_cast<float>(e.srcY) / th, static_cast<float>(e.srcW) / tw,
                    static_cast<float>(e.srcH) / th);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void RenderSystem::shutdown()
{
    if (sVBO != 0)
    {
        glDeleteBuffers(1, &sVBO);
        sVBO = 0;
    }
    if (sVAO != 0)
    {
        glDeleteVertexArrays(1, &sVAO);
        sVAO = 0;
    }
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
}
