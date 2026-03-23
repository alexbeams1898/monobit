#include "UIRenderer.h"

#include <cstring>
#include <glad/glad.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

// Vertex layout: pos(x,y) + uv(u,v) + color(r,g,b,a) = 8 floats.
static constexpr int FLOATS_PER_VERT = 8;
static constexpr int VERTS_PER_QUAD = 6; // two triangles
static constexpr int FLOATS_PER_QUAD = FLOATS_PER_VERT * VERTS_PER_QUAD;
static constexpr int MAX_QUADS = 4096;

// Shaders -- minimal vertex-color + texture.
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

uniform sampler2D uTexture;
uniform int uIsFont;

out vec4 fragColor;

void main()
{
    vec4 texel = texture(uTexture, vUV);
    if (uIsFont != 0)
    {
        // Font atlas is single-channel (red). Use red as alpha.
        fragColor = vec4(vColor.rgb, vColor.a * texel.r);
    }
    else
    {
        fragColor = texel * vColor;
    }
}
)glsl";

// Batch state.
struct Batch
{
    GLuint texture = 0;
    bool is_font = false;
    int quad_count = 0;
};

static GLuint sProgram = 0;
static GLuint sVAO = 0;
static GLuint sVBO = 0;
static GLuint sWhiteTex = 0;
static int sWindowW = 0;
static int sWindowH = 0;

static std::vector<float> sVertexData;
static std::vector<Batch> sBatches;
static GLuint sCurrentTex = 0;
static bool sCurrentIsFont = false;

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
        std::cerr << "[UIRenderer] Shader compile error:\n" << log << "\n";
    }
    return shader;
}

static void buildOrtho(float mat[16], float left, float right, float bottom, float top)
{
    const float rml = right - left;
    const float tmb = top - bottom;
    std::memset(mat, 0, 16 * sizeof(float));
    mat[0] = 2.0f / rml;
    mat[5] = 2.0f / tmb;
    mat[10] = -1.0f;
    mat[12] = -(right + left) / rml;
    mat[13] = -(top + bottom) / tmb;
    mat[15] = 1.0f;
}

static void pushQuad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                     const Color& c)
{
    // clang-format off
    const float verts[] = {
        x,     y,     u0, v0, c.r, c.g, c.b, c.a,
        x + w, y,     u1, v0, c.r, c.g, c.b, c.a,
        x + w, y + h, u1, v1, c.r, c.g, c.b, c.a,

        x,     y,     u0, v0, c.r, c.g, c.b, c.a,
        x + w, y + h, u1, v1, c.r, c.g, c.b, c.a,
        x,     y + h, u0, v1, c.r, c.g, c.b, c.a,
    };
    // clang-format on
    sVertexData.insert(sVertexData.end(), verts, verts + FLOATS_PER_QUAD);
}

static void ensureBatch(GLuint tex, bool is_font)
{
    if (sBatches.empty() || sCurrentTex != tex || sCurrentIsFont != is_font)
    {
        sBatches.push_back({tex, is_font, 0});
        sCurrentTex = tex;
        sCurrentIsFont = is_font;
    }
}

void UIRenderer::init(int window_w, int window_h)
{
    sWindowW = window_w;
    sWindowH = window_h;

    GLuint vert = compileShader(GL_VERTEX_SHADER, kVertSrc);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFragSrc);

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
        std::cerr << "[UIRenderer] Program link error:\n" << log << "\n";
    }

    glDeleteShader(vert);
    glDeleteShader(frag);

    // Dynamic VBO for batched quads.
    glGenVertexArrays(1, &sVAO);
    glGenBuffers(1, &sVBO);

    glBindVertexArray(sVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_QUADS * FLOATS_PER_QUAD * sizeof(float), nullptr,
                 GL_DYNAMIC_DRAW);

    // Position (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERT * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // UV (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, FLOATS_PER_VERT * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color (location 2)
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, FLOATS_PER_VERT * sizeof(float),
                          reinterpret_cast<void*>(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // 1x1 white texture for solid-color rects.
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &sWhiteTex);
    glBindTexture(GL_TEXTURE_2D, sWhiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    sVertexData.reserve(MAX_QUADS * FLOATS_PER_QUAD);
    sBatches.reserve(32);
}

void UIRenderer::resize(int window_w, int window_h)
{
    sWindowW = window_w;
    sWindowH = window_h;
}

void UIRenderer::shutdown()
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
    if (sWhiteTex != 0)
    {
        glDeleteTextures(1, &sWhiteTex);
        sWhiteTex = 0;
    }
}

void UIRenderer::beginFrame()
{
    sVertexData.clear();
    sBatches.clear();
    sCurrentTex = 0;
    sCurrentIsFont = false;
}

void UIRenderer::endFrame()
{
    ZoneScopedN("UIRenderer");

    if (sVertexData.empty())
        return;

    // Upload vertex data.
    glBindBuffer(GL_ARRAY_BUFFER, sVBO);
    const auto dataSize = static_cast<GLsizeiptr>(sVertexData.size() * sizeof(float));
    const auto bufSize = static_cast<GLsizeiptr>(MAX_QUADS * FLOATS_PER_QUAD * sizeof(float));
    if (dataSize > bufSize)
    {
        // Grow VBO if batch exceeds pre-allocated size.
        glBufferData(GL_ARRAY_BUFFER, dataSize, sVertexData.data(), GL_DYNAMIC_DRAW);
    }
    else
    {
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, sVertexData.data());
    }

    // Set up projection and state.
    float proj[16];
    buildOrtho(proj, 0.0f, static_cast<float>(sWindowW), static_cast<float>(sWindowH), 0.0f);

    glUseProgram(sProgram);
    glUniformMatrix4fv(glGetUniformLocation(sProgram, "uProjection"), 1, GL_FALSE, proj);
    glUniform1i(glGetUniformLocation(sProgram, "uTexture"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(sVAO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Flush batches.
    int offset = 0;
    for (const auto& batch : sBatches)
    {
        glBindTexture(GL_TEXTURE_2D, batch.texture);
        glUniform1i(glGetUniformLocation(sProgram, "uIsFont"), batch.is_font ? 1 : 0);
        const int verts = batch.quad_count * VERTS_PER_QUAD;
        glDrawArrays(GL_TRIANGLES, offset, verts);
        offset += verts;
    }

    glBindVertexArray(0);
    glUseProgram(0);
}

void UIRenderer::drawRect(float x, float y, float w, float h, const Color& color)
{
    ensureBatch(sWhiteTex, false);
    pushQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
    sBatches.back().quad_count++;
}

void UIRenderer::drawTexturedRect(float x, float y, float w, float h, uint32_t tex_id, float u0,
                                  float v0, float u1, float v1, const Color& tint)
{
    ensureBatch(tex_id, false);
    pushQuad(x, y, w, h, u0, v0, u1, v1, tint);
    sBatches.back().quad_count++;
}

float UIRenderer::drawText(FontHandle font, const std::string& text, float x, float y,
                           const Color& color)
{
    const uint32_t atlas = FontManager::atlasTexture(font);
    if (atlas == 0)
        return 0.0f;

    ensureBatch(atlas, true);

    // y = visual top of text line. Offset by ascent so glyph y_off (which is
    // relative to baseline) places glyphs correctly below the top.
    const float baseline = y + FontManager::ascent(font);

    float cursor_x = x;
    for (char ch : text)
    {
        const GlyphInfo* g = FontManager::glyph(font, ch);
        if (!g)
            continue;

        if (g->width > 0.0f && g->height > 0.0f)
        {
            const float gx = cursor_x + g->x_off;
            const float gy = baseline + g->y_off;
            pushQuad(gx, gy, g->width, g->height, g->u0, g->v0, g->u1, g->v1, color);
            sBatches.back().quad_count++;
        }
        cursor_x += g->advance;
    }
    return cursor_x - x;
}

TextSize UIRenderer::measureText(FontHandle font, const std::string& text)
{
    float w = 0.0f;
    float h = FontManager::lineHeight(font);
    for (char ch : text)
    {
        const GlyphInfo* g = FontManager::glyph(font, ch);
        if (g)
            w += g->advance;
    }
    return {w, h};
}

int UIRenderer::windowWidth()
{
    return sWindowW;
}

int UIRenderer::windowHeight()
{
    return sWindowH;
}
