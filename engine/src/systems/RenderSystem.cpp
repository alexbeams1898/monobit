#include "systems/RenderSystem.h"

#include "ecs/Components.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <iostream>
#include <tracy/Tracy.hpp>
#include <vector>

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
uniform vec4      uTint;    // (r, g, b, a) multiplied into the sampled color

out vec4 fragColor;

void main()
{
    vec2 uv = uSrcRect.xy + vUV * uSrcRect.zw;
    fragColor = texture(uTexture, uv) * uTint;
}
)glsl";

static GLuint sProgram = 0;
static GLuint sVAO = 0;
static GLuint sVBO = 0;
static GLuint sWhiteTex = 0;
static int sWindowW = 0;
static int sWindowH = 0;

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

static void buildOrtho(float mat[16], float left, float right, float bottom, float top)
{
    const float rml = right - left;
    const float tmb = top - bottom;

    // clang-format off
    mat[ 0] = 2.0f / rml;  mat[ 4] = 0.0f;         mat[ 8] = 0.0f;  mat[12] = -(right + left) / rml;
    mat[ 1] = 0.0f;        mat[ 5] = 2.0f / tmb;   mat[ 9] = 0.0f;  mat[13] = -(top + bottom) / tmb;
    mat[ 2] = 0.0f;        mat[ 6] = 0.0f;          mat[10] = -1.0f; mat[14] = 0.0f;
    mat[ 3] = 0.0f;        mat[ 7] = 0.0f;          mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

static void buildModel(float mat[16], float x, float y, float w, float h)
{
    // clang-format off
    mat[ 0] = w;     mat[ 4] = 0.0f;  mat[ 8] = 0.0f;  mat[12] = x;
    mat[ 1] = 0.0f;  mat[ 5] = h;     mat[ 9] = 0.0f;  mat[13] = y;
    mat[ 2] = 0.0f;  mat[ 6] = 0.0f;  mat[10] = 1.0f;  mat[14] = 0.0f;
    mat[ 3] = 0.0f;  mat[ 7] = 0.0f;  mat[11] = 0.0f;  mat[15] = 1.0f;
    // clang-format on
}

// Applies TintOverride if present, otherwise leaves tint at default white.
// TintSystem (game) owns all tint priority logic and clears/sets TintOverride each frame.
static void computeTint(EntityManager& em, entt::entity entity, float& tr, float& tg, float& tb)
{
    if (const auto* tint = em.registry().try_get<TintOverride>(entity))
    {
        tr = tint->r;
        tg = tint->g;
        tb = tint->b;
    }
}

void RenderSystem::init(int windowW, int windowH)
{
    sWindowW = windowW;
    sWindowH = windowH;

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

    // clang-format off
    const float vertices[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
    };
    // clang-format on

    glGenVertexArrays(1, &sVAO);
    glGenBuffers(1, &sVBO);

    glBindVertexArray(sVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &sWhiteTex);
    glBindTexture(GL_TEXTURE_2D, sWhiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void RenderSystem::resize(int windowW, int windowH)
{
    sWindowW = windowW;
    sWindowH = windowH;
    glViewport(0, 0, windowW, windowH);
}

static void buildSrcRect(int src_x, int src_y, int src_w, int src_h, int tex_w, int tex_h,
                         bool flip_x, bool flip_y, float& uvX, float& uvY, float& uvW, float& uvH)
{
    const float tw = static_cast<float>(tex_w > 0 ? tex_w : 1);
    const float th = static_cast<float>(tex_h > 0 ? tex_h : 1);
    uvX = flip_x ? static_cast<float>(src_x + src_w) / tw : static_cast<float>(src_x) / tw;
    uvW = flip_x ? -static_cast<float>(src_w) / tw : static_cast<float>(src_w) / tw;
    uvY = flip_y ? static_cast<float>(src_y + src_h) / th : static_cast<float>(src_y) / th;
    uvH = flip_y ? -static_cast<float>(src_h) / th : static_cast<float>(src_h) / th;
}

struct DrawEntry
{
    float x, y;
    float sort_y;
    int src_x, src_y, src_w, src_h;
    int layer;
    int sub_layer;
    uint32_t tex_id;
    int tex_w, tex_h;
    float tr, tg, tb;
    bool flip_x;
    bool flip_y;
    bool solid_color;
    float draw_scale;
    float ta;
    float glow_scale;
    float glow_alpha;
};

static bool buildSpriteDrawEntry(EntityManager& em, TextureManager& tm, entt::entity entity,
                                 const Transform& transform, const Sprite& sprite, float alpha,
                                 DrawEntry& out)
{
    const bool hasSolidColor = em.registry().all_of<SolidColor>(entity);
    if (sprite.texture_path.empty() && sprite.texture_id == 0 && !hasSolidColor)
        return false;

    const uint32_t tex_id = sprite.texture_path.empty() ? 0 : tm.load(sprite.texture_path);
    int tex_w = 0, tex_h = 0;
    if (!sprite.texture_path.empty())
        tm.getDimensions(sprite.texture_path, tex_w, tex_h);

    auto& reg = em.registry();
    const auto* bp = reg.try_get<BodyPart>(entity);

    float tr = 1.0f, tg = 1.0f, tb = 1.0f;
    float ta = 1.0f;
    entt::entity tintEntity = entity;
    if (bp && reg.valid(bp->parent))
        tintEntity = bp->parent;
    computeTint(em, tintEntity, tr, tg, tb);

    if (const auto* particle = reg.try_get<Particle>(entity))
    {
        float t = particle->age / particle->lifetime;
        ta = (1.0f - t) * (1.0f - t);
    }

    bool flip_x = false, flip_y = false;
    if (!reg.all_of<Animation>(entity) && reg.all_of<FacingDirection>(entity))
    {
        const auto& facing = reg.get<FacingDirection>(entity);
        flip_x = facing.render_dx < -0.1f;
        flip_y = facing.render_dy < -0.1f;
    }

    bool is_solid = false;
    if (reg.all_of<SolidColor>(entity))
    {
        const auto& sc = reg.get<SolidColor>(entity);
        tr = sc.r;
        tg = sc.g;
        tb = sc.b;
        is_solid = true;
    }

    float drawX = transform.x, drawY = transform.y;
    if (const auto* prev = reg.try_get<PreviousTransform>(entity))
    {
        drawX = prev->x + (transform.x - prev->x) * alpha;
        drawY = prev->y + (transform.y - prev->y) * alpha;
    }
    drawX = std::round(drawX);
    drawY = std::round(drawY);

    const float scale = transform.scale;

    const Collider* col = reg.try_get<Collider>(entity);
    if (!col && bp && reg.valid(bp->parent))
        col = reg.try_get<Collider>(bp->parent);

    float yOffset = 0.0f;
    if (col)
        yOffset = (static_cast<float>(sprite.src_h) * scale - col->height) * 0.5f;

    const float sortY = col ? drawY + col->height * 0.5f : drawY;
    const int subLayer = (bp && bp->direction_from_facing) ? 1 : 0;

    float glowScale = 0.0f;
    float glowAlpha = 0.0f;
    if (const auto* glow = reg.try_get<Glow>(entity))
    {
        glowScale = glow->scale;
        glowAlpha = glow->alpha;
    }

    out = {drawX - static_cast<float>(sprite.src_w) * scale * 0.5f,
           drawY - static_cast<float>(sprite.src_h) * scale * 0.5f - yOffset,
           sortY,
           sprite.src_x,
           sprite.src_y,
           sprite.src_w,
           sprite.src_h,
           sprite.layer,
           subLayer,
           tex_id,
           tex_w,
           tex_h,
           tr,
           tg,
           tb,
           flip_x,
           flip_y,
           is_solid,
           scale,
           ta,
           glowScale,
           glowAlpha};
    return true;
}

void RenderSystem::render(EntityManager& em, TextureManager& tm, float camX, float camY)
{
    ZoneScopedN("RenderSystem");
    const float alpha = em.render_alpha;

    std::vector<DrawEntry> drawList;
    drawList.reserve(64);

    for (auto [entity, transform, sprite] : em.registry().view<Transform, Sprite>().each())
    {
        DrawEntry entry{};
        if (buildSpriteDrawEntry(em, tm, entity, transform, sprite, alpha, entry))
            drawList.push_back(entry);
    }

    std::sort(drawList.begin(), drawList.end(),
              [](const DrawEntry& a, const DrawEntry& b)
              {
                  if (a.layer != b.layer)
                      return a.layer < b.layer;
                  if (a.sort_y != b.sort_y)
                      return a.sort_y < b.sort_y;
                  return a.sub_layer < b.sub_layer;
              });

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
        // Draw glow halo behind the entity (larger, semi-transparent).
        if (e.glow_scale > 0.0f)
        {
            const float gw = static_cast<float>(e.src_w) * e.draw_scale * e.glow_scale;
            const float gh = static_cast<float>(e.src_h) * e.draw_scale * e.glow_scale;
            const float baseW = static_cast<float>(e.src_w) * e.draw_scale;
            const float baseH = static_cast<float>(e.src_h) * e.draw_scale;
            const float gx = e.x - (gw - baseW) * 0.5f;
            const float gy = e.y - (gh - baseH) * 0.5f;
            float glowModel[16];
            buildModel(glowModel, gx, gy, gw, gh);
            glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, glowModel);
            glBindTexture(GL_TEXTURE_2D, sWhiteTex);
            glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), 0.0f, 0.0f, 1.0f, 1.0f);
            glUniform4f(glGetUniformLocation(sProgram, "uTint"), e.tr, e.tg, e.tb, e.glow_alpha);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }

        float model[16];
        buildModel(model, e.x, e.y, static_cast<float>(e.src_w) * e.draw_scale,
                   static_cast<float>(e.src_h) * e.draw_scale);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, model);

        if (e.solid_color)
        {
            glBindTexture(GL_TEXTURE_2D, sWhiteTex);
            glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), 0.0f, 0.0f, 1.0f, 1.0f);
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, e.tex_id);

            float uvX = 0.0f, uvY = 0.0f, uvW = 0.0f, uvH = 0.0f;
            buildSrcRect(e.src_x, e.src_y, e.src_w, e.src_h, e.tex_w, e.tex_h, e.flip_x, e.flip_y,
                         uvX, uvY, uvW, uvH);
            glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), uvX, uvY, uvW, uvH);
        }

        glUniform4f(glGetUniformLocation(sProgram, "uTint"), e.tr, e.tg, e.tb, e.ta);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Crosshair
    {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        const float worldX = snapCamX + static_cast<float>(mx) - halfW;
        const float worldY = snapCamY + static_cast<float>(my) - halfH;

        static constexpr float kArmLen = 6.0f;
        static constexpr float kThick = 2.0f;
        static constexpr float kGap = 2.0f;

        glBindTexture(GL_TEXTURE_2D, sWhiteTex);
        glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), 0.0f, 0.0f, 1.0f, 1.0f);
        glUniform4f(glGetUniformLocation(sProgram, "uTint"), 1.0f, 1.0f, 1.0f, 0.8f);

        float cModel[16];
        buildModel(cModel, worldX - kGap - kArmLen, worldY - kThick * 0.5f, kArmLen, kThick);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, cModel);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        buildModel(cModel, worldX + kGap, worldY - kThick * 0.5f, kArmLen, kThick);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, cModel);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        buildModel(cModel, worldX - kThick * 0.5f, worldY - kGap - kArmLen, kThick, kArmLen);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, cModel);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        buildModel(cModel, worldX - kThick * 0.5f, worldY + kGap, kThick, kArmLen);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, cModel);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Facing dot
    static constexpr float kDotSize = 6.0f;
    static constexpr float kDotOffset = 10.0f;

    glBindTexture(GL_TEXTURE_2D, sWhiteTex);
    glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), 0.0f, 0.0f, 1.0f, 1.0f);
    glUniform4f(glGetUniformLocation(sProgram, "uTint"), 0.0f, 0.0f, 0.0f, 1.0f);

    for (auto [entity, transform, facing] : em.registry().view<Transform, FacingDirection>().each())
    {
        if (em.registry().all_of<Animation>(entity) || !em.registry().all_of<Sprite>(entity))
            continue;
        float anchorX = std::round(transform.x);
        float anchorY = std::round(transform.y);
        if (const auto* prev = em.registry().try_get<PreviousTransform>(entity))
        {
            anchorX = std::round(prev->x + (transform.x - prev->x) * alpha);
            anchorY = std::round(prev->y + (transform.y - prev->y) * alpha);
        }

        const float dotX = std::round(anchorX + facing.render_dx * kDotOffset) - kDotSize * 0.5f;
        const float dotY = std::round(anchorY + facing.render_dy * kDotOffset) - kDotSize * 0.5f;
        float model[16];
        buildModel(model, dotX, dotY, kDotSize, kDotSize);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, model);
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
    if (sWhiteTex != 0)
    {
        glDeleteTextures(1, &sWhiteTex);
        sWhiteTex = 0;
    }
}
