#include "systems/RenderSystem.h"

#include "ecs/Components.h"
#include "gl/ShaderUtils.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <glad/glad.h>

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
uniform vec4      uSrcRect;   // (x, y, w, h) in 0-1 UV space
uniform vec4      uTint;      // (r, g, b, a) multiplied into the sampled color
uniform vec4      uOutline;   // (r, g, b, a) -- a > 0 puts this draw in OUTLINE mode
uniform vec2      uTexelStep; // one source texel in UV space (uSrcRect.zw / src_h_w px)
uniform float     uOutlineW;  // rim thickness in texels

out vec4 fragColor;

void main()
{
    vec2 uv = uSrcRect.xy + vUV * uSrcRect.zw;

    // Normal mode: textured quad tinted. Outline mode (uOutline.a > 0): this fragment is
    // part of the rim only if it is TRANSPARENT here but a neighbor within uOutlineW texels
    // is opaque -- i.e. we're just OUTSIDE the sprite's silhouette. Sample 8 directions.
    if (uOutline.a <= 0.0)
    {
        fragColor = texture(uTexture, uv) * uTint;
        return;
    }
    if (texture(uTexture, uv).a > 0.02)
        discard; // inside the shape -- the sprite pass draws here, not the rim
    vec2 s = uTexelStep * uOutlineW;
    float near = 0.0;
    near = max(near, texture(uTexture, uv + vec2( s.x, 0.0)).a);
    near = max(near, texture(uTexture, uv + vec2(-s.x, 0.0)).a);
    near = max(near, texture(uTexture, uv + vec2(0.0,  s.y)).a);
    near = max(near, texture(uTexture, uv + vec2(0.0, -s.y)).a);
    near = max(near, texture(uTexture, uv + vec2( s.x,  s.y)).a);
    near = max(near, texture(uTexture, uv + vec2( s.x, -s.y)).a);
    near = max(near, texture(uTexture, uv + vec2(-s.x,  s.y)).a);
    near = max(near, texture(uTexture, uv + vec2(-s.x, -s.y)).a);
    if (near <= 0.02)
        discard; // no opaque neighbor -> not on the rim
    fragColor = vec4(uOutline.rgb, uOutline.a * near);
}
)glsl";

static GLuint sProgram = 0;
static GLuint sVAO = 0;
static GLuint sVBO = 0;
static GLuint sWhiteTex = 0;
static int sWindowW = 0;
static int sWindowH = 0;

// Cached uniform locations -- avoid per-draw glGetUniformLocation string lookups.
static GLint sLocProjection = -1;
static GLint sLocModel = -1;
static GLint sLocTexture = -1;
static GLint sLocSrcRect = -1;
static GLint sLocTint = -1;
static GLint sLocOutline = -1;   // (r,g,b,a); a>0 = outline-mode draw
static GLint sLocTexelStep = -1; // one source texel in UV space
static GLint sLocOutlineW = -1;  // rim thickness in texels

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

    const GLuint vert = engine::gl::compileShader(GL_VERTEX_SHADER, kVertexShaderSrc);
    const GLuint frag = engine::gl::compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);

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

    sLocProjection = glGetUniformLocation(sProgram, "uProjection");
    sLocModel = glGetUniformLocation(sProgram, "uModel");
    sLocTexture = glGetUniformLocation(sProgram, "uTexture");
    sLocSrcRect = glGetUniformLocation(sProgram, "uSrcRect");
    sLocTint = glGetUniformLocation(sProgram, "uTint");
    sLocOutline = glGetUniformLocation(sProgram, "uOutline");
    sLocTexelStep = glGetUniformLocation(sProgram, "uTexelStep");
    sLocOutlineW = glGetUniformLocation(sProgram, "uOutlineW");
}

void RenderSystem::resize(int windowW, int windowH)
{
    sWindowW = windowW;
    sWindowH = windowH;
    glViewport(0, 0, windowW, windowH);
}

// Inputs to buildSrcRect: source-texture rect + total texture dims + flips.
struct SrcRectInput
{
    int src_x, src_y, src_w, src_h;
    int tex_w, tex_h;
    bool flip_x, flip_y;
};
// Output UV rect (origin + extents; extents may be negative when flipped).
struct SrcRectUV
{
    float x, y, w, h;
};
// Half-texel inset so GL_NEAREST always samples texel centers, not edges.
// Non-power-of-2 textures (e.g. 3328px) produce repeating-decimal UVs in
// float32 that can round to the adjacent texel at certain draw scales.
static SrcRectUV buildSrcRect(const SrcRectInput& in)
{
    const float tw = static_cast<float>(in.tex_w > 0 ? in.tex_w : 1);
    const float th = static_cast<float>(in.tex_h > 0 ? in.tex_h : 1);
    constexpr float HALF = 0.5f;
    const float x0 = (static_cast<float>(in.src_x) + HALF) / tw;
    const float x1 = (static_cast<float>(in.src_x + in.src_w) - HALF) / tw;
    const float y0 = (static_cast<float>(in.src_y) + HALF) / th;
    const float y1 = (static_cast<float>(in.src_y + in.src_h) - HALF) / th;
    SrcRectUV out;
    out.x = in.flip_x ? x1 : x0;
    out.w = in.flip_x ? -(x1 - x0) : (x1 - x0);
    out.y = in.flip_y ? y1 : y0;
    out.h = in.flip_y ? -(y1 - y0) : (y1 - y0);
    return out;
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
    float rotation;
    bool geo_mirror_x;
    float outline_r, outline_g, outline_b, outline_a; // outline_a > 0 => draw the rim
    float outline_w;                                  // rim thickness in texels
};

// Resolve texture id + dimensions for a sprite entity. Returns the
// runtime tex id; writes dims via out params. Pre-baked-composite
// path derives dims from Animation layout.
static uint32_t resolveTextureBinding(EntityManager& em, TextureManager& tm, entt::entity entity,
                                      const Sprite& sprite, int& tex_w, int& tex_h)
{
    const uint32_t tex_id =
        sprite.texture_path.empty() ? sprite.texture_id : tm.load(sprite.texture_path);
    if (!sprite.texture_path.empty())
        tm.getDimensions(sprite.texture_path, tex_w, tex_h);
    else if (const auto* anim = em.registry().try_get<Animation>(entity))
    {
        tex_w = anim->frame_width * anim->max_frames_per_state * anim->direction_count;
        tex_h = anim->frame_height * anim->row_count;
    }
    return tex_id;
}

// Tint result: tr/tg/tb/ta + whether SolidColor took over (renders as
// untextured fill in the draw loop).
struct TintResult
{
    float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
    bool is_solid = false;
};
static TintResult resolveTint(EntityManager& em, entt::entity entity)
{
    TintResult r;
    computeTint(em, entity, r.tr, r.tg, r.tb);
    auto& reg = em.registry();
    if (const auto* particle = reg.try_get<Particle>(entity))
    {
        const float t = particle->age / particle->lifetime;
        r.ta = (1.0f - t) * (1.0f - t);
    }
    if (reg.all_of<SolidColor>(entity))
    {
        const auto& sc = reg.get<SolidColor>(entity);
        r.tr = sc.r;
        r.tg = sc.g;
        r.tb = sc.b;
        r.is_solid = true;
    }
    return r;
}

// Resolve flip flags: sprite-default unless overridden by FacingDirection.
static void resolveFlips(EntityManager& em, entt::entity entity, const Sprite& sprite, bool& flip_x,
                         bool& flip_y)
{
    auto& reg = em.registry();
    flip_x = sprite.flip_x;
    flip_y = false;
    if (!reg.all_of<Animation>(entity) && reg.all_of<FacingDirection>(entity))
    {
        const auto& facing = reg.get<FacingDirection>(entity);
        flip_x = facing.render_dx < -0.1f;
        flip_y = facing.render_dy < -0.1f;
    }
}

// Interpolate transform position toward the next frame for sub-tick
// smoothness; rounds to whole pixels.
static void resolveInterpolatedPos(EntityManager& em, entt::entity entity,
                                   const Transform& transform, float alpha, float& drawX,
                                   float& drawY)
{
    drawX = transform.x;
    drawY = transform.y;
    if (const auto* prev = em.registry().try_get<PreviousTransform>(entity))
    {
        drawX = prev->x + (transform.x - prev->x) * alpha;
        drawY = prev->y + (transform.y - prev->y) * alpha;
    }
    drawX = std::round(drawX);
    drawY = std::round(drawY);
}

// Glow extras: scale + alpha if a Glow component is attached.
static void resolveGlow(EntityManager& em, entt::entity entity, float& glowScale, float& glowAlpha)
{
    glowScale = 0.0f;
    glowAlpha = 0.0f;
    if (const auto* glow = em.registry().try_get<Glow>(entity))
    {
        glowScale = glow->scale;
        glowAlpha = glow->alpha;
    }
}

// Outline rim: color + width if an Outline component is attached (alpha 0 = none).
static void resolveOutline(EntityManager& em, entt::entity entity, DrawEntry& out)
{
    out.outline_a = 0.0f;
    if (const auto* o = em.registry().try_get<Outline>(entity))
    {
        out.outline_r = o->r;
        out.outline_g = o->g;
        out.outline_b = o->b;
        out.outline_a = o->alpha;
        out.outline_w = o->width;
    }
}

static bool buildSpriteDrawEntry(EntityManager& em, TextureManager& tm, entt::entity entity,
                                 const Transform& transform, const Sprite& sprite, float alpha,
                                 DrawEntry& out)
{
    const bool hasSolidColor = em.registry().all_of<SolidColor>(entity);
    if (sprite.texture_path.empty() && sprite.texture_id == 0 && !hasSolidColor)
        return false;

    int tex_w = 0, tex_h = 0;
    const uint32_t tex_id = resolveTextureBinding(em, tm, entity, sprite, tex_w, tex_h);
    const TintResult tint = resolveTint(em, entity);
    bool flip_x = false, flip_y = false;
    resolveFlips(em, entity, sprite, flip_x, flip_y);

    float drawX = 0.0f, drawY = 0.0f;
    resolveInterpolatedPos(em, entity, transform, alpha, drawX, drawY);

    const float scale = transform.scale;
    const Collider* col = em.registry().try_get<Collider>(entity);
    const float yOffset =
        col ? (static_cast<float>(sprite.src_h) * scale - col->height) * 0.5f : 0.0f;
    const float sortY =
        sprite.use_sort_anchor ? sprite.sort_anchor : (col ? drawY + col->height * 0.5f : drawY);

    float glowScale = 0.0f, glowAlpha = 0.0f;
    resolveGlow(em, entity, glowScale, glowAlpha);

    out = {drawX - static_cast<float>(sprite.src_w) * scale * 0.5f,
           drawY - static_cast<float>(sprite.src_h) * scale * 0.5f - yOffset,
           sortY,
           sprite.src_x,
           sprite.src_y,
           sprite.src_w,
           sprite.src_h,
           sprite.layer,
           sprite.sub_layer,
           tex_id,
           tex_w,
           tex_h,
           tint.tr,
           tint.tg,
           tint.tb,
           flip_x,
           flip_y,
           tint.is_solid,
           scale,
           tint.ta * sprite.alpha,
           glowScale,
           glowAlpha,
           sprite.rotation,
           sprite.geo_mirror_x};
    resolveOutline(em, entity, out); // outline_a stays 0 (no rim) unless an Outline is present
    return true;
}

void RenderSystem::render(EntityManager& em, TextureManager& tm, float camX, float camY, float zoom)
{
    ZoneScopedN("RenderSystem");
    const float alpha = em.render_alpha;

    std::vector<DrawEntry> drawList;
    {
        ZoneScopedN("RenderSystem::buildDrawList");
        drawList.reserve(64);

        for (auto [entity, transform, sprite] : em.registry().view<Transform, Sprite>().each())
        {
            DrawEntry entry{};
            if (buildSpriteDrawEntry(em, tm, entity, transform, sprite, alpha, entry))
                drawList.push_back(entry);
        }
    }

    std::sort(drawList.begin(), drawList.end(),
              [](const DrawEntry& a, const DrawEntry& b)
              {
                  if (a.layer != b.layer)
                      return a.layer < b.layer;
                  const float dy = a.sort_y - b.sort_y;
                  if (dy < -0.5f || dy > 0.5f)
                      return dy < 0.0f;
                  return a.sub_layer < b.sub_layer;
              });

    const float snapCamX = std::round(camX);
    const float snapCamY = std::round(camY);
    const float halfW = static_cast<float>(sWindowW) * 0.5f / zoom;
    const float halfH = static_cast<float>(sWindowH) * 0.5f / zoom;
    float proj[16];
    engine::gl::buildOrtho(proj, snapCamX - halfW, snapCamX + halfW, snapCamY + halfH,
                           snapCamY - halfH);

    glUseProgram(sProgram);
    glUniformMatrix4fv(sLocProjection, 1, GL_FALSE, proj);
    glUniform1i(sLocTexture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(sVAO);

    {
        ZoneScopedN("RenderSystem::drawSprites");
        for (const auto& e : drawList)
        {
            glUniform4f(sLocOutline, 0.0f, 0.0f, 0.0f, 0.0f); // normal mode unless the rim below

            // Rim outline pass: draw the sprite's texture into a quad enlarged by the rim
            // width, in outline mode -- the shader emits color only just OUTSIDE the opaque
            // silhouette, so a lit edge hugs the shape. Before the sprite so the sprite draws
            // on top of its own rim. Textured sprites only (a solid-color fill has no shape).
            if (e.outline_a > 0.0f && !e.solid_color && e.tex_w > 0 && e.tex_h > 0)
            {
                const float sw = static_cast<float>(e.src_w) * e.draw_scale;
                const float sh = static_cast<float>(e.src_h) * e.draw_scale;
                const float padX = e.outline_w * e.draw_scale; // rim room, in world px
                const float padY = e.outline_w * e.draw_scale;
                float outModel[16];
                engine::gl::buildModel(outModel, e.x - padX, e.y - padY, sw + 2.0f * padX,
                                       sh + 2.0f * padY);
                glUniformMatrix4fv(sLocModel, 1, GL_FALSE, outModel);
                glBindTexture(GL_TEXTURE_2D, e.tex_id);
                // One source texel in full-texture UV (always positive; independent of any
                // src-rect flip). The enlarged quad maps a src rect grown by outline_w texels
                // on every side, so its extra border samples the transparent margin AROUND the
                // sprite -- exactly where the rim is emitted.
                const float uStep = 1.0f / static_cast<float>(e.tex_w);
                const float vStep = 1.0f / static_cast<float>(e.tex_h);
                const SrcRectUV uv = buildSrcRect(
                    {e.src_x, e.src_y, e.src_w, e.src_h, e.tex_w, e.tex_h, e.flip_x, e.flip_y});
                // Grow the (possibly flipped) rect outward: expand along each axis's sign so
                // the sampled region always covers src +/- outline_w texels.
                const float sgnU = uv.w < 0.0f ? -1.0f : 1.0f;
                const float sgnV = uv.h < 0.0f ? -1.0f : 1.0f;
                glUniform4f(sLocSrcRect, uv.x - sgnU * uStep * e.outline_w,
                            uv.y - sgnV * vStep * e.outline_w,
                            uv.w + sgnU * 2.0f * uStep * e.outline_w,
                            uv.h + sgnV * 2.0f * vStep * e.outline_w);
                glUniform2f(sLocTexelStep, uStep, vStep);
                glUniform1f(sLocOutlineW, e.outline_w);
                glUniform4f(sLocOutline, e.outline_r, e.outline_g, e.outline_b, e.outline_a);
                glUniform4f(sLocTint, 1.0f, 1.0f, 1.0f, 1.0f);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
                glUniform4f(sLocOutline, 0.0f, 0.0f, 0.0f, 0.0f); // back to normal mode
            }

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
                engine::gl::buildModel(glowModel, gx, gy, gw, gh);
                glUniformMatrix4fv(sLocModel, 1, GL_FALSE, glowModel);
                glBindTexture(GL_TEXTURE_2D, sWhiteTex);
                glUniform4f(sLocSrcRect, 0.0f, 0.0f, 1.0f, 1.0f);
                glUniform4f(sLocTint, e.tr, e.tg, e.tb, e.glow_alpha);
                glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            }

            float model[16];
            const float spriteW = static_cast<float>(e.src_w) * e.draw_scale;
            const float spriteH = static_cast<float>(e.src_h) * e.draw_scale;
            if (e.rotation != 0.0f || e.geo_mirror_x)
                engine::gl::buildModelRotated(model, e.x, e.y, spriteW, spriteH, e.rotation,
                                              e.geo_mirror_x);
            else
                engine::gl::buildModel(model, e.x, e.y, spriteW, spriteH);
            glUniformMatrix4fv(sLocModel, 1, GL_FALSE, model);

            if (e.solid_color)
            {
                glBindTexture(GL_TEXTURE_2D, sWhiteTex);
                glUniform4f(sLocSrcRect, 0.0f, 0.0f, 1.0f, 1.0f);
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, e.tex_id);

                const SrcRectUV uv = buildSrcRect(
                    {e.src_x, e.src_y, e.src_w, e.src_h, e.tex_w, e.tex_h, e.flip_x, e.flip_y});
                glUniform4f(sLocSrcRect, uv.x, uv.y, uv.w, uv.h);
            }

            glUniform4f(sLocTint, e.tr, e.tg, e.tb, e.ta);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        }
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
