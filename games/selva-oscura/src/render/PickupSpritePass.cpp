#include "render/PickupSpritePass.h"

#include "WallClock.h"
#include "ecs/Items.h"
#include "gl/ShaderUtils.h"
#include "items/ItemRegistry.h"
#include "loot/Pickups.h"
#include "render/PickupMeshPass.h"

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

// World-space sprite half-size in meters. The glow exists to read as
// AMBIENT LIGHT catching the item -- a subtle highlight at close
// range, like the way a candle reflects off objects sitting near it.
// NOT a navigation marker. The actual item's MESH is the discovery
// surface; the glow is just the "light wash" that makes it readable.
constexpr float kBaseSize = 0.25f;

// Distance-falloff: full opacity at <= kNearDistance, linearly fades
// to zero at kFarDistance, invisible beyond. Aggressive falloff so
// the glow only contributes at close range -- you should not see
// pickups from afar via the glow alone (the world mesh is what reads
// at distance).
constexpr float kNearDistance = 3.0f;
constexpr float kFarDistance = 12.0f;

// Peak alpha cap. Even at full brightness the glow stays subtle so
// it reads as reflected light, not an emissive sign. Rarity scaling
// (glowFor) multiplies this; legendary items can approach 1.0, but
// VeryCommon / Common materials stay barely-perceptible at all
// distances.
constexpr float kAlphaCeiling = 0.35f;

// Subtle pulse so static-camera pickups still read as alive.
constexpr float kPulseAmplitude = 0.10f;
constexpr float kPulseHz = 1.1f;

constexpr float kQuadVerts[] = {
    -1.0f, -1.0f, +1.0f, -1.0f, -1.0f, +1.0f, +1.0f, +1.0f,
};

const char* kVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aLocalPos;

uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;

uniform vec4 uPickupPosSize[128];
uniform vec4 uPickupColor[128];

out vec2 vUv;
flat out vec4 vColor;

void main()
{
    int idx = gl_InstanceID;
    vec3 center = uPickupPosSize[idx].xyz;
    float size = uPickupPosSize[idx].w;

    vec3 worldPos = center + uCamRight * aLocalPos.x * size
                            + uCamUp * aLocalPos.y * size;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vUv = aLocalPos;
    vColor = uPickupColor[idx];
}
)glsl";

const char* kFS = R"glsl(
#version 330 core
in vec2 vUv;
flat in vec4 vColor;

out vec4 fragColor;

void main()
{
    float r = length(vUv);
    if (r >= 1.0)
        discard;
    float a = cos(r * 1.5707963);
    a = a * a;
    fragColor = vec4(vColor.rgb * a * vColor.a, a * vColor.a);
}
)glsl";

GLuint sProgram = 0;
GLuint sVao = 0;
GLuint sVbo = 0;
GLint sUniViewProjLoc = -1;
GLint sUniCamRightLoc = -1;
GLint sUniCamUpLoc = -1;
GLint sUniPosSizeLoc = -1;
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
    switch (r)
    {
    case engine::ecs::Rarity::VeryCommon:
        return {0.45f, 0.42f, 0.55f};
    case engine::ecs::Rarity::Common:
        return {0.55f, 0.48f, 0.72f};
    case engine::ecs::Rarity::Uncommon:
        return {0.55f, 0.40f, 0.85f};
    case engine::ecs::Rarity::Rare:
        return {0.65f, 0.30f, 0.95f};
    case engine::ecs::Rarity::Epic:
        return {0.80f, 0.30f, 1.00f};
    case engine::ecs::Rarity::Legendary:
        return {0.95f, 0.85f, 1.00f};
    }
    return {0.55f, 0.48f, 0.72f};
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

// Per-rarity size + alpha scaling. Higher rarities = visibly larger
// glow. Quality nudges both upward at the high tiers. Same shape as
// the prior glowFor() table but rebalanced for world-space units
// (meters, not pixels).
struct GlowSize
{
    float size_mult; // multiplier on kBaseSize
    float alpha;     // 0..1 max alpha at near distance
};

GlowSize glowFor(engine::ecs::Rarity r, engine::ecs::QualityTier q)
{
    GlowSize gs{};
    gs.size_mult = 1.0f;
    gs.alpha = 0.55f;
    if (r >= engine::ecs::Rarity::Common)
    {
        gs.size_mult = 1.1f;
        gs.alpha = 0.65f;
    }
    if (r >= engine::ecs::Rarity::Uncommon)
    {
        gs.size_mult = 1.3f;
        gs.alpha = 0.75f;
    }
    if (r >= engine::ecs::Rarity::Rare)
    {
        gs.size_mult = 1.6f;
        gs.alpha = 0.85f;
    }
    if (r >= engine::ecs::Rarity::Epic)
    {
        gs.size_mult = 1.9f;
        gs.alpha = 0.95f;
    }
    if (r >= engine::ecs::Rarity::Legendary)
    {
        gs.size_mult = 2.2f;
        gs.alpha = 1.00f;
    }
    if (q >= engine::ecs::QualityTier::Fine)
        gs.size_mult = std::max(gs.size_mult, 1.2f);
    if (q >= engine::ecs::QualityTier::Superior)
        gs.size_mult = std::max(gs.size_mult, 1.5f);
    if (q >= engine::ecs::QualityTier::Masterwork)
        gs.size_mult = std::max(gs.size_mult, 1.8f);
    return gs;
}

} // namespace

bool initPickupSpritePass()
{
    sProgram = engine::gl::compileProgram(kVS, kFS);
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniCamRightLoc = glGetUniformLocation(sProgram, "uCamRight");
    sUniCamUpLoc = glGetUniformLocation(sProgram, "uCamUp");
    sUniPosSizeLoc = glGetUniformLocation(sProgram, "uPickupPosSize");
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

    const auto& items = selva::items::itemRegistry();

    float pos_size[kMaxPickups * 4];
    float color[kMaxPickups * 4];
    int n = 0;
    for (const auto& p : pickups)
    {
        if (n >= kMaxPickups)
            break;
        // Skip pickups that render as a world mesh -- the mesh IS
        // the visual; layering a glow on top would defeat the
        // "blends with terrain" point. Weapons + special drops
        // (no world_mesh) still get the sprite. EXCEPTION: if the
        // mesh path has been attempted and failed to load (asset
        // missing, parse error, etc), fall through to the sprite as
        // a fallback so the pickup is still visible to the player.
        // Without this, a broken world_mesh path = invisible pickup.
        const engine::ecs::ItemDef* def = items.find(p.item.config_path);
        if (def != nullptr && !def->world_mesh.empty() &&
            !selva::render::isPickupMeshLoadFailed(def->world_mesh))
            continue;
        const glm::vec3 live = selva::loot::livePickupPos(p);
        const float dist = glm::length(live - cam_pos);

        // Distance falloff: 1.0 at <= near, linearly down to 0.0 at far,
        // clipped beyond. Pickup goes from clearly visible (close) to
        // invisible (far) -- discovery-by-proximity, not navigation
        // beacon.
        float falloff = 1.0f;
        if (dist > kNearDistance)
        {
            if (dist >= kFarDistance)
                continue; // skip drawing entirely beyond far range
            falloff = 1.0f - (dist - kNearDistance) / (kFarDistance - kNearDistance);
        }

        const Rgb base = rarityColor(p.rarity);
        const float bright = qualityBrightness(p.quality);
        const GlowSize gs = glowFor(p.rarity, p.quality);

        const float size = kBaseSize * gs.size_mult * pulse;
        // Premultiplied-style: shader multiplies vColor.rgb * vColor.a.
        // Send color * brightness (clamped), and alpha = max alpha *
        // falloff so distance fades the whole sprite.
        pos_size[n * 4 + 0] = live.x;
        pos_size[n * 4 + 1] = live.y;
        pos_size[n * 4 + 2] = live.z;
        pos_size[n * 4 + 3] = size;
        color[n * 4 + 0] = std::min(1.0f, base.r * bright);
        color[n * 4 + 1] = std::min(1.0f, base.g * bright);
        color[n * 4 + 2] = std::min(1.0f, base.b * bright);
        color[n * 4 + 3] = std::min(kAlphaCeiling, gs.alpha * kAlphaCeiling) * falloff;
        ++n;
    }
    if (n == 0)
        return;

    // Derive camera right/up from view_proj inverse, matching
    // LightSpritePass. Single inverse per frame for one pass is fine
    // at this scale.
    const glm::mat4 inv_vp = glm::inverse(view_proj);
    auto unproject = [&](float ndc_x, float ndc_y) -> glm::vec3
    {
        const glm::vec4 p = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
        return glm::vec3(p) / p.w;
    };
    const glm::vec3 p_center = unproject(0.0f, 0.0f);
    const glm::vec3 p_right = unproject(1.0f, 0.0f);
    const glm::vec3 p_up = unproject(0.0f, 1.0f);
    glm::vec3 cam_right = p_right - p_center;
    glm::vec3 cam_up = p_up - p_center;
    const float rlen = glm::length(cam_right);
    const float ulen = glm::length(cam_up);
    if (rlen > 1e-6f)
        cam_right /= rlen;
    if (ulen > 1e-6f)
        cam_up /= ulen;

    glUseProgram(sProgram);
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
    glUniform3f(sUniCamRightLoc, cam_right.x, cam_right.y, cam_right.z);
    glUniform3f(sUniCamUpLoc, cam_up.x, cam_up.y, cam_up.z);
    glUniform4fv(sUniPosSizeLoc, n, pos_size);
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
