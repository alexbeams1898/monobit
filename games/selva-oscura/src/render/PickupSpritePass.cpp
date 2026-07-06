#include "render/PickupSpritePass.h"

#include "WallClock.h"
#include "ecs/Items.h"
#include "gl/ShaderUtils.h"
#include "loot/Pickups.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

constexpr int kMaxPickups = 128;

// Vertical pillar of light, world-up axis, billboarded around it so
// it always faces the camera horizontally. Elden-Ring drop convention:
// the pillar IS the discovery cue, visible across the area; the mesh
// (if any) is the confirmation when you walk up. Heights / widths
// scale with rarity.
constexpr float kBaseHalfWidth = 0.18f; // half-width of the quad in meters
constexpr float kBaseHeight = 1.6f;     // full pillar height in meters (ground -> top)
constexpr float kGroundLift = 0.02f;    // tiny lift so the pillar base isn't z-fighting the floor

// Distance falloff: pillar is fully bright within kNearDistance, fades
// linearly to kFarDistance, culled beyond. Tuned to be readable across
// a Selva clearing -- you should be able to spot a drop from a fair
// distance, that's the whole point.
constexpr float kNearDistance = 12.0f;
constexpr float kFarDistance = 60.0f;

// Peak alpha. Pillar is meant to be obvious, not subtle -- raise
// rarities cap near 1.0, even base rarities read clearly at close
// range.
constexpr float kAlphaCeiling = 0.90f;

// Subtle pulse so static-camera pickups still read as alive.
constexpr float kPulseAmplitude = 0.10f;
constexpr float kPulseHz = 1.1f;

// Two triangles forming a quad spanning local X = [-1,+1] and local Y
// = [0,1]. Local Y is mapped to WORLD UP in the vertex shader so the
// pillar grows from the ground; local X billboards toward the camera.
constexpr float kQuadVerts[] = {
    -1.0f, 0.0f, +1.0f, 0.0f, -1.0f, 1.0f, +1.0f, 1.0f,
};

// Pillar of light: aLocalPos.x = [-1,+1] across the pillar's
// horizontal extent (billboarded toward camera in the horizontal
// plane only), aLocalPos.y = [0,1] from ground -> top along world-up.
// uPickupPosSize.xyz = ground-anchor position (foot of pillar).
// uPickupPosSize.w   = horizontal half-width in meters.
// uPickupShape.x     = pillar height in meters.
// uPickupShape.y     = ground lift (small offset so base isn't
//                       z-fighting the floor).
const char* kVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aLocalPos;

uniform mat4 uViewProj;
uniform vec3 uCamRightHoriz;

uniform vec4 uPickupPosSize[128];
uniform vec4 uPickupShape[128];
uniform vec4 uPickupColor[128];

out vec2 vUv;
flat out vec4 vColor;

void main()
{
    int idx = gl_InstanceID;
    vec3 ground = uPickupPosSize[idx].xyz;
    float half_width = uPickupPosSize[idx].w;
    float height = uPickupShape[idx].x;
    float lift = uPickupShape[idx].y;

    vec3 horiz = uCamRightHoriz * aLocalPos.x * half_width;
    vec3 up = vec3(0.0, 1.0, 0.0) * (aLocalPos.y * height + lift);
    vec3 worldPos = ground + horiz + up;

    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vUv = aLocalPos;
    vColor = uPickupColor[idx];
}
)glsl";

// Pillar shading: hot core at the centerline (|u| near 0), soft falloff
// to the sides; vertical profile holds brightness near the ground and
// tapers to a point at the top (the ER "fading-to-the-sky" look).
const char* kFS = R"glsl(
#version 330 core
in vec2 vUv;
flat in vec4 vColor;

out vec4 fragColor;

void main()
{
    float u = vUv.x;             // [-1, +1] across pillar width
    float v = clamp(vUv.y, 0.0, 1.0); // [0, 1] from ground to top

    // Horizontal: cos^2 profile gives a hot core, soft edges. discard
    // outside [-1,+1] is implicit from the quad.
    float horiz = cos(u * 1.5707963);
    horiz = horiz * horiz;

    // Vertical taper: hold ~1.0 for the lower portion, then fall off
    // smoothly toward 0 at the top. smoothstep(1, 0.2, v) gives full
    // brightness near v=0.2 and dies by v=1.0. This is the "candle
    // flame" silhouette ER drops use -- thick at the base, point at
    // the top.
    float vert = smoothstep(1.0, 0.2, v);

    float a = horiz * vert;
    fragColor = vec4(vColor.rgb * a * vColor.a, a * vColor.a);
}
)glsl";

GLuint sProgram = 0;
GLuint sVao = 0;
GLuint sVbo = 0;
GLint sUniViewProjLoc = -1;
GLint sUniCamRightHorizLoc = -1;
GLint sUniPosSizeLoc = -1;
GLint sUniShapeLoc = -1;
GLint sUniColorLoc = -1;

struct Rgb
{
    float r;
    float g;
    float b;
};

// Per-rarity base color. Purple gradient ramp -- substance carries
// the cool wash even at tier 0; higher rarities push toward saturated
// purple, then near-white at Legendary.
Rgb rarityColor(engine::ecs::Rarity r)
{
    // Linear-space; originally sRGB-authored (purple ramp from
    // muted-lavender to near-white-blue). Linearized via the IEC
    // 61966-2-1 transfer curve so the sRGB-framebuffer round-trip
    // produces the same on-screen tier-color the author tuned for.
    switch (r)
    {
    case engine::ecs::Rarity::VeryCommon:
        return {0.1706f, 0.1473f, 0.2633f};
    case engine::ecs::Rarity::Common:
        return {0.2633f, 0.1960f, 0.4770f};
    case engine::ecs::Rarity::Uncommon:
        return {0.2633f, 0.1329f, 0.6921f};
    case engine::ecs::Rarity::Rare:
        return {0.3801f, 0.0732f, 0.8900f};
    case engine::ecs::Rarity::Epic:
        return {0.6038f, 0.0732f, 1.0000f};
    case engine::ecs::Rarity::Legendary:
        return {0.8900f, 0.6921f, 1.0000f};
    }
    return {0.2633f, 0.1960f, 0.4770f};
}

// Quality boosts brightness of the rarity color (Crude dims toward
// 0.75x, Masterwork pushes 1.5x). Clamped at write to 1.0 per channel.
float qualityBrightness(engine::ecs::QualityTier q)
{
    switch (q)
    {
    case engine::ecs::QualityTier::Crude:
        return 0.75f;
    case engine::ecs::QualityTier::Common:
        return 1.00f;
    case engine::ecs::QualityTier::Fine:
        return 1.15f;
    case engine::ecs::QualityTier::Superior:
        return 1.30f;
    case engine::ecs::QualityTier::Masterwork:
        return 1.50f;
    }
    return 1.0f;
}

// Per-rarity pillar shape + alpha. Higher rarities = taller + wider
// + brighter pillar. Quality nudges height upward at high tiers (a
// Masterwork Common reads almost as obvious as an Uncommon).
struct PillarShape
{
    float height_mult; // multiplier on kBaseHeight
    float width_mult;  // multiplier on kBaseHalfWidth
    float alpha;       // 0..1 max alpha at near distance
};

PillarShape pillarFor(engine::ecs::Rarity r, engine::ecs::QualityTier q)
{
    PillarShape ps{};
    ps.height_mult = 1.0f;
    ps.width_mult = 1.0f;
    ps.alpha = 0.70f;
    if (r >= engine::ecs::Rarity::Common)
    {
        ps.height_mult = 1.15f;
        ps.alpha = 0.78f;
    }
    if (r >= engine::ecs::Rarity::Uncommon)
    {
        ps.height_mult = 1.35f;
        ps.width_mult = 1.1f;
        ps.alpha = 0.85f;
    }
    if (r >= engine::ecs::Rarity::Rare)
    {
        ps.height_mult = 1.6f;
        ps.width_mult = 1.2f;
        ps.alpha = 0.92f;
    }
    if (r >= engine::ecs::Rarity::Epic)
    {
        ps.height_mult = 1.9f;
        ps.width_mult = 1.35f;
        ps.alpha = 0.97f;
    }
    if (r >= engine::ecs::Rarity::Legendary)
    {
        ps.height_mult = 2.3f;
        ps.width_mult = 1.5f;
        ps.alpha = 1.00f;
    }
    if (q >= engine::ecs::QualityTier::Fine)
        ps.height_mult = std::max(ps.height_mult, 1.2f);
    if (q >= engine::ecs::QualityTier::Superior)
        ps.height_mult = std::max(ps.height_mult, 1.5f);
    if (q >= engine::ecs::QualityTier::Masterwork)
        ps.height_mult = std::max(ps.height_mult, 1.8f);
    return ps;
}

} // namespace

bool initPickupSpritePass()
{
    sProgram = engine::gl::compileProgram(kVS, kFS);
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniCamRightHorizLoc = glGetUniformLocation(sProgram, "uCamRightHoriz");
    sUniPosSizeLoc = glGetUniformLocation(sProgram, "uPickupPosSize");
    sUniShapeLoc = glGetUniformLocation(sProgram, "uPickupShape");
    sUniColorLoc = glGetUniformLocation(sProgram, "uPickupColor");

    glGenVertexArrays(1, &sVao);
    glGenBuffers(1, &sVbo);
    glBindVertexArray(sVao);
    glBindBuffer(GL_ARRAY_BUFFER, sVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return true;
}

void shutdownPickupSpritePass()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    if (sVbo != 0)
    {
        glDeleteBuffers(1, &sVbo);
        sVbo = 0;
    }
    if (sVao != 0)
    {
        glDeleteVertexArrays(1, &sVao);
        sVao = 0;
    }
}

void renderPickupSprites(const glm::mat4& view_proj, const glm::vec3& cam_pos)
{
    const auto& pickups = selva::loot::allPickups();
    if (pickups.empty() || sProgram == 0)
        return;

    const float t = selva::wallClock();
    const float pulse = 1.0f + kPulseAmplitude * std::sin(t * kPulseHz * 6.2831853f);

    float pos_size[kMaxPickups * 4];
    float shape[kMaxPickups * 4];
    float color[kMaxPickups * 4];
    int n = 0;
    for (const auto& p : pickups)
    {
        if (n >= kMaxPickups)
            break;
        // Every pickup gets a pillar -- mesh-rendered items (Wood
        // materials) get the pillar layered on top of the mesh; items
        // without a world_mesh (weapons, special drops) get only the
        // pillar.

        const glm::vec3 live = selva::loot::livePickupPos(p);
        const float dist = glm::length(live - cam_pos);

        float falloff = 1.0f;
        if (dist > kNearDistance)
        {
            if (dist >= kFarDistance)
                continue; // beyond far range -- cull
            falloff = 1.0f - (dist - kNearDistance) / (kFarDistance - kNearDistance);
        }

        const Rgb base = rarityColor(p.rarity);
        const float bright = qualityBrightness(p.quality);
        const PillarShape ps = pillarFor(p.rarity, p.quality);

        const float half_width = kBaseHalfWidth * ps.width_mult;
        const float height = kBaseHeight * ps.height_mult * pulse;

        // Ground anchor: foot of the pillar. The mesh / floor sits
        // at live.y, so the pillar grows from there +kGroundLift.
        pos_size[n * 4 + 0] = live.x;
        pos_size[n * 4 + 1] = live.y;
        pos_size[n * 4 + 2] = live.z;
        pos_size[n * 4 + 3] = half_width;

        shape[n * 4 + 0] = height;
        shape[n * 4 + 1] = kGroundLift;
        shape[n * 4 + 2] = 0.0f;
        shape[n * 4 + 3] = 0.0f;

        // Premultiplied-style: shader multiplies vColor.rgb * vColor.a.
        color[n * 4 + 0] = std::min(1.0f, base.r * bright);
        color[n * 4 + 1] = std::min(1.0f, base.g * bright);
        color[n * 4 + 2] = std::min(1.0f, base.b * bright);
        color[n * 4 + 3] = std::min(kAlphaCeiling, ps.alpha * kAlphaCeiling) * falloff;
        ++n;
    }
    if (n == 0)
        return;

    // Camera-right projected onto the horizontal (XZ) plane. This is
    // the billboard axis: the pillar rotates around its world-up to
    // always present its broad face to the camera, but never tilts
    // off vertical -- a pillar viewed from above is foreshortened to
    // a thin column, not a flat disc.
    const glm::mat4 inv_vp = glm::inverse(view_proj);
    auto unproject = [&](float ndc_x, float ndc_y) -> glm::vec3
    {
        const glm::vec4 p = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
        return glm::vec3(p) / p.w;
    };
    const glm::vec3 p_center = unproject(0.0f, 0.0f);
    const glm::vec3 p_right = unproject(1.0f, 0.0f);
    glm::vec3 cam_right_horiz = p_right - p_center;
    cam_right_horiz.y = 0.0f;
    const float rlen = glm::length(cam_right_horiz);
    if (rlen > 1e-6f)
        cam_right_horiz /= rlen;

    glUseProgram(sProgram);
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
    glUniform3f(sUniCamRightHorizLoc, cam_right_horiz.x, cam_right_horiz.y, cam_right_horiz.z);
    glUniform4fv(sUniPosSizeLoc, n, pos_size);
    glUniform4fv(sUniShapeLoc, n, shape);
    glUniform4fv(sUniColorLoc, n, color);

    // Additive blend; depth-test ON (occluded by geometry); depth-
    // write OFF (sprite doesn't occlude geometry behind it). Same
    // contract as LightSpritePass.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

    glBindVertexArray(sVao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    (void)cam_pos;
}

} // namespace selva::render
