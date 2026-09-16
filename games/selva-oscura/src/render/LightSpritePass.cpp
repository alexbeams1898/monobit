#include "render/LightSpritePass.h"

#include "WallClock.h"
#include "gl/ShaderUtils.h"
#include "world/Lights.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Must match MAX_LIGHTS used elsewhere (TerrainShader / RegionShaders).
constexpr int kMaxLights = 64;

// Sprite world-space half-size in meters. Scales with light radius so
// big radius → big visible flame, small radius → small flicker.
// kSizePerRadius=1.0 means a light with radius=10m has a 10m sprite.
// Tuned in iteration; flame should read as "object" at close range
// without dominating the view, and as "bright point" at far range.
constexpr float kSizePerRadius = 0.20f;
constexpr float kMinSize = 0.5f;

// Two-triangle quad in local sprite space: [-1,-1] to [+1,+1] UV.
constexpr float kQuadVerts[] = {
    -1.0f, -1.0f, +1.0f, -1.0f, -1.0f, +1.0f, +1.0f, +1.0f,
};

const char* kVS = R"glsl(
#version 330 core
layout(location = 0) in vec2 aLocalPos;

uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;

// Per-light data (instanced). xyz = world position, w = sprite size.
uniform vec4 uLightPosSize[64];
// xyz = color * intensity (premultiplied). w = unused.
uniform vec4 uLightColor[64];

out vec2 vUv;
flat out vec3 vColor;

void main()
{
    int idx = gl_InstanceID;
    vec3 center = uLightPosSize[idx].xyz;
    float size = uLightPosSize[idx].w;

    // Camera-facing billboard: offset along camera-right + camera-up
    // by aLocalPos × size. Each instance gets its own world-space
    // size from uLightPosSize[i].w.
    vec3 worldPos = center + uCamRight * aLocalPos.x * size
                            + uCamUp * aLocalPos.y * size;
    gl_Position = uViewProj * vec4(worldPos, 1.0);
    vUv = aLocalPos; // [-1, +1] for both axes
    vColor = uLightColor[idx].rgb;
}
)glsl";

const char* kFS = R"glsl(
#version 330 core
in vec2 vUv;
flat in vec3 vColor;

out vec4 fragColor;

void main()
{
    // Radial falloff. r=0 at center, r=1 at corners. Squared-cosine
    // bell shape reads as a soft glow (not a hard disc, not a
    // gaussian smear that fades to nothing visible).
    float r = length(vUv);
    if (r >= 1.0)
        discard;
    float a = cos(r * 1.5707963);  // pi/2 — cos(0)=1, cos(pi/2)=0
    a = a * a;

    // Output color premultiplied by alpha; additive blend on the
    // GL side means this directly adds light to the framebuffer.
    fragColor = vec4(vColor * a, a);
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

} // namespace

bool initLightSpritePass()
{
    sProgram = engine::gl::compileProgram(kVS, kFS);
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniCamRightLoc = glGetUniformLocation(sProgram, "uCamRight");
    sUniCamUpLoc = glGetUniformLocation(sProgram, "uCamUp");
    sUniPosSizeLoc = glGetUniformLocation(sProgram, "uLightPosSize");
    sUniColorLoc = glGetUniformLocation(sProgram, "uLightColor");

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

void shutdownLightSpritePass()
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

void renderLightSprites(const glm::mat4& view_proj, const glm::vec3& cam_pos,
                        const char* region_name)
{
    const auto& all_lights = engine::world::allLights();
    if (all_lights.empty())
        return;

    // Filter to the requested region (or all when region_name=nullptr).
    // Region-tagged lights with mismatching region are dropped; globals
    // (region_name=nullptr in the LightSource) match every region.
    const float t = selva::wallClock();
    float pos_size[kMaxLights * 4];
    float color[kMaxLights * 4];
    int n = 0;
    for (size_t i = 0; i < all_lights.size(); ++i)
    {
        const auto& L = all_lights[i];
        if (n >= kMaxLights)
            break;
        if (region_name != nullptr && L.region_name != nullptr &&
            std::strcmp(region_name, L.region_name) != 0)
            continue;
        // Modulated intensity drives BOTH the sprite brightness AND
        // the point-light contribution to the ground (uploaded from
        // TerrainShader.cpp / RegionShaders.cpp using the same call).
        // Keeps visible flame brightness and floor illumination in
        // sync.
        const float live_intensity = engine::world::flickerIntensity(static_cast<int>(i), t);
        const float size = std::max(kMinSize, kSizePerRadius * L.radius);
        pos_size[n * 4 + 0] = L.position.x;
        pos_size[n * 4 + 1] = L.position.y;
        pos_size[n * 4 + 2] = L.position.z;
        pos_size[n * 4 + 3] = size;
        color[n * 4 + 0] = L.color.x * live_intensity;
        color[n * 4 + 1] = L.color.y * live_intensity;
        color[n * 4 + 2] = L.color.z * live_intensity;
        color[n * 4 + 3] = 1.0f;
        ++n;
    }
    if (n == 0)
        return;

    // Camera-right + camera-up extracted from inverse view. View is
    // stored row-major (glm column-major actually — column 0 = right
    // vector, column 1 = up). Easier path: recompute from the view
    // matrix's rotation rows. Skip the math by reading view_proj's
    // implicit basis is complex; the caller should pass world right/up
    // explicitly via the shader uniforms.
    //
    // For sprite billboarding we want the camera's local right and up
    // axes in world space. These come from the inverse of the view
    // matrix's upper-left 3x3 (column 0 = world-space right, column 1 =
    // world-space up). Approximate via gluLookAt convention without
    // re-storing the view: compute from view_proj's columns is wrong
    // because perspective mixes them. Instead, accept that this pass
    // expects the call site to expose right/up — pass them in.
    //
    // BANDAID(approved): right+up are derived by inverting view_proj.
    // Why: the pass needs the camera's screen-right and screen-up
    // basis, but the caller (renderWorld) only passes view_proj. The
    // root fix is to thread the inverse-view (or right+up) through
    // the call site so we don't pay an inverse-mat4 per frame. The
    // inverse is cheap enough on one matrix that the current
    // structure ships fine; revisit if multiple light passes appear.
    //
    // Cleanest: extract right + up directly from view matrix. Caller
    // provides view_proj, we invert and read columns 0 + 1.
    const glm::mat4 inv_vp = glm::inverse(view_proj);
    // World-space points at NDC (1,0,0) and (-1,0,0) → screen-right vs
    // screen-left. Their world-space difference is the world-right axis.
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

    // Additive blend: light sprites brighten what's behind them
    // (atop terrain / region / sky). Depth test stays on so lights are
    // occluded by walls + terrain in front, but don't write depth so
    // they don't occlude geometry behind them.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDepthMask(GL_FALSE);

    glBindVertexArray(sVao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n);
    glBindVertexArray(0);

    // Restore opaque blend + depth write for subsequent passes.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    (void)cam_pos;
}

} // namespace selva::render
