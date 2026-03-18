#include "systems/RenderSystem.h"

#include "ecs/Components.h"

#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <iostream>
#include <tracy/Tracy.hpp>
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
uniform vec4      uTint;    // (r, g, b, a) multiplied into the sampled color

out vec4 fragColor;

void main()
{
    vec2 uv = uSrcRect.xy + vUV * uSrcRect.zw;
    fragColor = texture(uTexture, uv) * uTint;
}
)glsl";

// ---------------------------------------------------------------------------
// Static state — kept in this translation unit; accessed only through the
// public static methods. Fine for a single-window game.
// ---------------------------------------------------------------------------

static GLuint sProgram = 0;
static GLuint sVAO = 0;
static GLuint sVBO = 0;
static GLuint sWhiteTex = 0; // 1×1 white texture used for solid-color primitives (facing dot)
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
// Internal helpers
// ---------------------------------------------------------------------------

// Compute tint RGB for an entity based on its current status components.
// Priority: damage flash > attack flash > staggered > level-up ready > weapon cooldown.
static void computeTint(EntityManager& em, entt::entity entity, float& tr, float& tg, float& tb)
{
    if (em.registry().all_of<DamageFeedback>(entity))
    {
        tr = 0.2f;
        tg = 0.4f; // blue
    }
    else if (em.registry().all_of<AttackFeedback>(entity))
    {
        tg = 0.5f;
        tb = 0.0f; // orange
    }
    else if (em.registry().all_of<Staggered>(entity))
    {
        tr = 0.6f;
        tg = 0.0f;
        tb = 1.0f; // purple
    }
    else if (em.registry().all_of<Experience>(entity) &&
             em.registry().get<Experience>(entity).stat_points > 0)
    {
        tr = 1.0f;
        tg = 0.9f;
        tb = 0.0f; // gold
    }
    else if (em.registry().all_of<Weapon>(entity))
    {
        // Fade smoothly from dim blue-grey (just swung) back to white (ready).
        const float cooldown = em.registry().get<Weapon>(entity).swing_cooldown_remaining;
        if (cooldown > 0.0f)
        {
            const float t = std::min(cooldown, 1.0f);
            tr = 1.0f - t * 0.35f; // 1.0 → 0.65
            tg = 1.0f - t * 0.35f;
            tb = 1.0f - t * 0.15f; // 1.0 → 0.85
        }
    }
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

    // 1×1 white RGBA texture — used to draw solid-color quads (e.g. facing dot)
    // without needing a dedicated atlas region.
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &sWhiteTex);
    glBindTexture(GL_TEXTURE_2D, sWhiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Converts a pixel src rect to normalized UV (x, y, w, h) for atlas sampling.
// Flip trick: start at far edge with negative extent — shader mirrors the sample.
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

void RenderSystem::render(EntityManager& em, TextureManager& tm, float camX, float camY)
{
    ZoneScopedN("RenderSystem");
    const float alpha = em.render_alpha;

    // Collect all renderable entities into a vector so we can sort by layer.
    struct DrawEntry
    {
        float x, y;
        int src_x, src_y, src_w, src_h;
        int layer;
        uint32_t tex_id;
        int tex_w, tex_h;                      // full texture dimensions for UV normalisation
        float tr = 1.0f, tg = 1.0f, tb = 1.0f; // tint RGB (multiplied in shader)
        bool flip_x = false;                   // mirror sprite horizontally (facing left)
        bool flip_y = false;                   // mirror sprite vertically (facing up = back view)
        bool solid_color = false;              // if true, render as flat color using white tex
        float draw_scale = 1.0f;               // uniform scale for the model matrix only
    };

    std::vector<DrawEntry> drawList;
    drawList.reserve(64);

    for (auto [entity, transform, sprite] : em.registry().view<Transform, Sprite>().each())
    {
        if (sprite.texture_path.empty() && sprite.texture_id == 0)
            continue;

        uint32_t tex_id = tm.load(sprite.texture_path);

        // Query texture dimensions so we can convert pixel src rects to 0-1 UV.
        glBindTexture(GL_TEXTURE_2D, tex_id);
        GLint tex_w = 0;
        GLint tex_h = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tex_w);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &tex_h);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Tint priority: damage flash > attack flash > staggered > level-up ready > cooldown.
        // Orange + blue are safe for red-green color blindness.
        float tr = 1.0f, tg = 1.0f, tb = 1.0f;
        computeTint(em, entity, tr, tg, tb);

        // Flip sprite based on facing direction.
        // flip_x: facing left  → mirror horizontally.
        // flip_y: facing up    → mirror vertically (back-of-character view).
        bool flip_x = false;
        bool flip_y = false;
        if (em.registry().all_of<FacingDirection>(entity))
        {
            const auto& facing = em.registry().get<FacingDirection>(entity);
            flip_x = facing.render_dx < -0.1f;
            flip_y = facing.render_dy < -0.1f;
        }

        // SolidColor overrides texture rendering with a flat colored square.
        bool is_solid = false;
        if (em.registry().all_of<SolidColor>(entity))
        {
            const auto& sc = em.registry().get<SolidColor>(entity);
            tr = sc.r;
            tg = sc.g;
            tb = sc.b;
            is_solid = true;
        }

        // Interpolate between previous and current position, then snap to the
        // integer pixel grid. The camera is integer-snapped too — without matching
        // grids, sprites oscillate ±0.5px relative to the camera each frame.
        float drawX = transform.x;
        float drawY = transform.y;
        if (const auto* prev = em.registry().try_get<PreviousTransform>(entity))
        {
            drawX = prev->x + (transform.x - prev->x) * alpha;
            drawY = prev->y + (transform.y - prev->y) * alpha;
        }
        drawX = std::round(drawX);
        drawY = std::round(drawY);

        const float sc = transform.scale;
        drawList.push_back({drawX - static_cast<float>(sprite.src_w) * sc * 0.5f, // top-left corner
                            drawY - static_cast<float>(sprite.src_h) * sc * 0.5f, sprite.src_x,
                            sprite.src_y, sprite.src_w, sprite.src_h, sprite.layer, tex_id, tex_w,
                            tex_h, tr, tg, tb, flip_x, flip_y, is_solid, sc});
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
        // Model matrix: position at (e.x, e.y), scale to (src_w, src_h) pixels.
        float model[16];
        buildModel(model, e.x, e.y, static_cast<float>(e.src_w) * e.draw_scale,
                   static_cast<float>(e.src_h) * e.draw_scale);
        glUniformMatrix4fv(glGetUniformLocation(sProgram, "uModel"), 1, GL_FALSE, model);

        if (e.solid_color)
        {
            // Flat color: sample the 1×1 white texture, tint to the entity's color.
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

        glUniform4f(glGetUniformLocation(sProgram, "uTint"), e.tr, e.tg, e.tb, 1.0f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }

    // Facing dot — 6×6 black square drawn at the front edge of every entity
    // that has a FacingDirection component.  Uses the 1×1 white texture tinted black.
    // Offset = 10 px in facing direction from center; dot top-left is 3 px back from that.
    static constexpr float kDotSize = 6.0f;
    static constexpr float kDotOffset = 10.0f;

    glBindTexture(GL_TEXTURE_2D, sWhiteTex);
    glUniform4f(glGetUniformLocation(sProgram, "uSrcRect"), 0.0f, 0.0f, 1.0f, 1.0f);
    glUniform4f(glGetUniformLocation(sProgram, "uTint"), 0.0f, 0.0f, 0.0f, 1.0f); // black

    for (auto [entity, transform, facing] : em.registry().view<Transform, FacingDirection>().each())
    {
        float anchorX = std::round(transform.x);
        float anchorY = std::round(transform.y);
        if (const auto* prev = em.registry().try_get<PreviousTransform>(entity))
        {
            anchorX = std::round(prev->x + (transform.x - prev->x) * alpha);
            anchorY = std::round(prev->y + (transform.y - prev->y) * alpha);
        }

        // render_dx/dy is smoothed toward the gameplay facing — fluid rotation
        // that filters mouse micro-tremor without robotic snapping.
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
