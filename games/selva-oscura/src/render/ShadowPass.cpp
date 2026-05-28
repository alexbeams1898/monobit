#include "render/ShadowPass.h"

#include "Tunables.h"
#include "gl/ShaderUtils.h"
#include "render/Atmosphere.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Depth-only vertex shaders. One per geometry family because each has
// its own input layout / transform model. All share the same single
// uniform mat4 uLightViewProj. The terrain VS has aPos in world space
// already (Terrain.cpp bakes world positions); scene + tree + skeletal
// each multiply by their own uModel; skeletal additionally applies the
// bone palette.

const char* kTerrainDepthVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightViewProj;
out vec3 vWorldPos;
void main()
{
    vWorldPos = aPos;
    gl_Position = uLightViewProj * vec4(aPos, 1.0);
}
)glsl";

// Terrain depth FS mirrors the main TerrainShader's structure-
// footprint discard so structure interiors don't get false self-
// shadowing from a terrain shadow caster the main pass doesn't draw.
// Same rect-array layout as TerrainShader; WorldRenderer uploads
// the same per-region cuts_floor rects to both programs.
const char* kTerrainDepthFS = R"glsl(
#version 330 core
in vec3 vWorldPos;
#define MAX_DISCARDS 16
uniform vec4 uDiscardRects[MAX_DISCARDS];
uniform int uDiscardCount;
void main()
{
    for (int i = 0; i < uDiscardCount; ++i)
    {
        vec2 center = uDiscardRects[i].xy;
        vec2 half_ext = uDiscardRects[i].zw;
        vec2 d = abs(vWorldPos.xz - center);
        if (d.x < half_ext.x && d.y < half_ext.y)
            discard;
    }
}
)glsl";

const char* kSceneDepthVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
void main()
{
    gl_Position = uLightViewProj * uModel * vec4(aPos, 1.0);
}
)glsl";

// Trees have alpha-cutout branches (leaf cards). For correct
// shadow shape we need to sample the base texture and discard low-
// alpha fragments. Same wind sway as the main VS or the shadow
// motion desyncs from the visible motion.
const char* kTreeDepthVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
uniform float uTime;
uniform float uWindPhase;
out vec2 vUV;
void main()
{
    // Wind sway intentionally DISABLED in the depth pass. The main
    // pass animates leaves visually (uTime drives sin), but at
    // distance the sway amplitude (~5-10cm at canopy height) exceeds
    // the soft shadow edge width (~6cm with 9-tap PCF), so per-frame
    // sway shows up as visible shadow stutter on far trees. Static
    // shadow casters are the standard rendering compromise - the
    // canopy still moves, the shadow stays steady. Cost: shadows
    // and visible canopy diverge slightly at high wind, only
    // perceptible if you stare at one specific leaf cluster.
    vec3 localPos = aPos;
    vec4 worldPos = uModel * vec4(localPos, 1.0);
    gl_Position = uLightViewProj * worldPos;
    vUV = aUV;
}
)glsl";

const char* kTreeDepthFS = R"glsl(
#version 330 core
in vec2 vUV;
uniform sampler2D uBaseColor;
uniform float uAlphaCutoff;
void main()
{
    vec4 base = texture(uBaseColor, vUV);
    if (base.a < uAlphaCutoff) discard;
}
)glsl";

const char* kSkeletalDepthVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;
uniform mat4 uModel;
uniform mat4 uBones[128];
uniform mat4 uLightViewProj;
void main()
{
    mat4 skin =
        aBoneWeights.x * uBones[aBoneIndices.x] +
        aBoneWeights.y * uBones[aBoneIndices.y] +
        aBoneWeights.z * uBones[aBoneIndices.z] +
        aBoneWeights.w * uBones[aBoneIndices.w];
    gl_Position = uLightViewProj * uModel * skin * vec4(aPos, 1.0);
}
)glsl";

// Minimal pass-through FS for opaque depth casters. We can't use
// glDrawBuffer(GL_NONE) alone because some drivers still link an FS;
// the simplest path is a no-op FS that lets the depth pipeline run.
const char* kPassthroughFS = R"glsl(
#version 330 core
void main() {}
)glsl";

GLuint sTerrainDepthProgram = 0;
GLint sTerrainDepthLVP = -1;
GLint sTerrainDepthDiscardRectsLoc = -1;
GLint sTerrainDepthDiscardCountLoc = -1;
constexpr int kMaxDepthDiscards = 16; // mirror kTerrainDepthFS MAX_DISCARDS
bool sDepthDiscardOverflowWarned = false;

GLuint sSceneDepthProgram = 0;
GLint sSceneDepthLVP = -1;
GLint sSceneDepthModel = -1;

GLuint sTreeDepthProgram = 0;
GLint sTreeDepthLVP = -1;
GLint sTreeDepthModel = -1;
GLint sTreeDepthTime = -1;
GLint sTreeDepthWindPhase = -1;
GLint sTreeDepthBaseColor = -1;
GLint sTreeDepthAlphaCutoff = -1;

GLuint sSkeletalDepthProgram = 0;
GLint sSkeletalDepthLVP = -1;
GLint sSkeletalDepthModel = -1;
GLint sSkeletalDepthBones = -1;

// Shadow map resolution + box half-extent. Texel size = 2*half/res.
// 4096 / half=80m = 3.9cm/texel. Larger box than the textbook
// "tight box around the camera" because:
//  - Far casters (trees 30-70m away) stay inside the box as the
//    player walks, so they don't pop in/out of casting shadows.
//    Visible symptom of pop-in: "ground darkens as I walk past a
//    far tree."
//  - 9-tap PCF (~12cm soft edge at this texel size) hides the
//    resolution loss vs a 40m box.
// Combined with distance-based shadow fade in ShadowShader.h, the
// fade-out band (60-80m) absorbs whatever sub-texel content change
// still happens at the box's true edge.
constexpr int kShadowMapSize = 4096;
constexpr float kOrthoHalfXY = 80.0f;
constexpr float kOrthoNear = -200.0f;
constexpr float kOrthoFar = 200.0f;

GLuint sDepthFBO = 0;
GLuint sDepthTex = 0;
glm::mat4 sLightViewProj = glm::mat4(1.0f);

// Diagnostic log. Opened lazily on first update; throttled to
// kLogIntervalFrames so the file doesn't balloon. Captures the most
// useful per-frame state for understanding what the shadow camera
// is doing and why edges look the way they do.
FILE* sLog = nullptr;
bool sLogOpenAttempted = false;
int sFrameCount = 0;
constexpr int kLogIntervalFrames = 30;

FILE* shadowLog()
{
    if (!selva::tuning::current().debug_shadow_log)
        return nullptr;
    if (sLog != nullptr)
        return sLog;
    if (sLogOpenAttempted)
        return nullptr;
    sLogOpenAttempted = true;
    sLog = std::fopen("shadow-debug.log", "w");
    if (sLog != nullptr)
    {
        const float texel = 2.0f * kOrthoHalfXY / static_cast<float>(kShadowMapSize);
        std::fprintf(sLog,
                     "# shadow pass trace\n"
                     "# map_size=%dx%d ortho_half=%.1fm texel=%.4fm (%.1fcm)\n"
                     "# ortho_near=%.1f ortho_far=%.1f\n"
                     "# per-frame rows: t,player=(x,y,z),sun=(x,y,z),"
                     "ls_player=(x,y),snap_offset=(x,y)\n",
                     kShadowMapSize, kShadowMapSize, kOrthoHalfXY, texel, texel * 100.0f,
                     kOrthoNear, kOrthoFar);
        std::fflush(sLog);
    }
    return sLog;
}

} // namespace

bool initShadowPass()
{
    glGenFramebuffers(1, &sDepthFBO);
    glGenTextures(1, &sDepthTex);

    glBindTexture(GL_TEXTURE_2D, sDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowMapSize, kShadowMapSize, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Clamp to border with a far depth value so off-map samples count
    // as fully lit (otherwise edges of the shadow box would self-shadow).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    constexpr float kBorderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, kBorderColor);
    // Enable hardware shadow comparison so sampler2DShadow in the
    // shader returns a 0/1 (lit/shadowed) result with hardware PCF.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glBindFramebuffer(GL_FRAMEBUFFER, sDepthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sDepthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "[shadow] FBO incomplete: 0x%x\n", status);
        if (FILE* f = shadowLog())
            std::fprintf(f, "[event] init FAILED - FBO incomplete: 0x%x\n", status);
        return false;
    }

    sTerrainDepthProgram = engine::gl::compileProgram(kTerrainDepthVS, kTerrainDepthFS);
    sSceneDepthProgram = engine::gl::compileProgram(kSceneDepthVS, kPassthroughFS);
    sTreeDepthProgram = engine::gl::compileProgram(kTreeDepthVS, kTreeDepthFS);
    sSkeletalDepthProgram = engine::gl::compileProgram(kSkeletalDepthVS, kPassthroughFS);
    if (sTerrainDepthProgram == 0 || sSceneDepthProgram == 0 || sTreeDepthProgram == 0 ||
        sSkeletalDepthProgram == 0)
    {
        std::fprintf(stderr, "[shadow] depth program compile failed\n");
        return false;
    }

    sTerrainDepthLVP = glGetUniformLocation(sTerrainDepthProgram, "uLightViewProj");
    sTerrainDepthDiscardRectsLoc = glGetUniformLocation(sTerrainDepthProgram, "uDiscardRects");
    sTerrainDepthDiscardCountLoc = glGetUniformLocation(sTerrainDepthProgram, "uDiscardCount");
    sSceneDepthLVP = glGetUniformLocation(sSceneDepthProgram, "uLightViewProj");
    sSceneDepthModel = glGetUniformLocation(sSceneDepthProgram, "uModel");
    sTreeDepthLVP = glGetUniformLocation(sTreeDepthProgram, "uLightViewProj");
    sTreeDepthModel = glGetUniformLocation(sTreeDepthProgram, "uModel");
    sTreeDepthTime = glGetUniformLocation(sTreeDepthProgram, "uTime");
    sTreeDepthWindPhase = glGetUniformLocation(sTreeDepthProgram, "uWindPhase");
    sTreeDepthBaseColor = glGetUniformLocation(sTreeDepthProgram, "uBaseColor");
    sTreeDepthAlphaCutoff = glGetUniformLocation(sTreeDepthProgram, "uAlphaCutoff");
    sSkeletalDepthLVP = glGetUniformLocation(sSkeletalDepthProgram, "uLightViewProj");
    sSkeletalDepthModel = glGetUniformLocation(sSkeletalDepthProgram, "uModel");
    sSkeletalDepthBones = glGetUniformLocation(sSkeletalDepthProgram, "uBones");
    return true;
}

void shutdownShadowPass()
{
    if (sDepthFBO != 0)
    {
        glDeleteFramebuffers(1, &sDepthFBO);
        sDepthFBO = 0;
    }
    if (sDepthTex != 0)
    {
        glDeleteTextures(1, &sDepthTex);
        sDepthTex = 0;
    }
}

void updateShadowCamera(const glm::vec3& player_world_pos)
{
    // Correct texel-grid snap.
    //
    // The previous attempt projected the player position through a
    // lookAt(eye, target=player, ...) which by definition puts the
    // target at view-space origin (0,0,0). Snapping zero to a texel
    // boundary is always zero - the snap was a no-op.
    //
    // The right approach: project the player onto the light-space
    // basis WITHOUT building a lookAt-at-player view. Use a fixed
    // world-anchored light frame (eye on world origin + sun offset)
    // and project the player's world coords into that frame. The
    // light-space XY there is non-zero and varies with player pos,
    // so the snap math actually works. Then construct the final view
    // matrix using the SNAPPED target.
    const glm::vec3 sun_dir = atmosphere::sunDirection();
    const glm::vec3 up =
        (std::abs(sun_dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float texel_size = 2.0f * kOrthoHalfXY / static_cast<float>(kShadowMapSize);

    // World-anchored light frame: look from world origin + sun toward
    // world origin. Used purely to extract the light-space basis.
    const glm::vec3 anchor_eye = sun_dir * 100.0f;
    const glm::mat4 anchor_view = glm::lookAt(anchor_eye, glm::vec3(0.0f), up);

    // Player position in this anchor frame. XY here are real values,
    // not always-zero.
    const glm::vec4 player_in_anchor = anchor_view * glm::vec4(player_world_pos, 1.0f);
    const float snapped_lx = std::round(player_in_anchor.x / texel_size) * texel_size;
    const float snapped_ly = std::round(player_in_anchor.y / texel_size) * texel_size;
    const glm::vec2 ls_snap_delta(snapped_lx - player_in_anchor.x, snapped_ly - player_in_anchor.y);

    // Map the (xy-only) snap delta back to world space via the anchor
    // view's inverse rotation. The Z component stays 0 - snapping in
    // depth would change which fragments self-occlude and isn't part
    // of the standard recipe.
    const glm::mat3 anchor_rot_inv = glm::transpose(glm::mat3(anchor_view));
    const glm::vec3 world_snap_delta =
        anchor_rot_inv * glm::vec3(ls_snap_delta.x, ls_snap_delta.y, 0.0f);

    const glm::vec3 snapped_target = player_world_pos + world_snap_delta;
    const glm::vec3 snapped_eye = snapped_target + sun_dir * 100.0f;
    const glm::mat4 snapped_view = glm::lookAt(snapped_eye, snapped_target, up);
    const glm::mat4 light_proj =
        glm::ortho(-kOrthoHalfXY, kOrthoHalfXY, -kOrthoHalfXY, kOrthoHalfXY, kOrthoNear, kOrthoFar);
    sLightViewProj = light_proj * snapped_view;

    if ((sFrameCount % kLogIntervalFrames) == 0)
    {
        if (FILE* f = shadowLog())
        {
            std::fprintf(f,
                         "frame=%d player=(%.2f,%.2f,%.2f) sun=(%.3f,%.3f,%.3f) "
                         "player_in_anchor=(%.3f,%.3f) ls_snap_delta=(%.4f,%.4f) "
                         "world_snap_delta=(%.4f,%.4f,%.4f) snapped_target=(%.2f,%.2f,%.2f) "
                         "texel=%.4fm\n",
                         sFrameCount, player_world_pos.x, player_world_pos.y, player_world_pos.z,
                         sun_dir.x, sun_dir.y, sun_dir.z, player_in_anchor.x, player_in_anchor.y,
                         ls_snap_delta.x, ls_snap_delta.y, world_snap_delta.x, world_snap_delta.y,
                         world_snap_delta.z, snapped_target.x, snapped_target.y, snapped_target.z,
                         texel_size);
            std::fflush(f);
        }
    }
    ++sFrameCount;
}

void beginDepthPass()
{
    // Don't query GL state per frame. glGetIntegerv(GL_VIEWPORT) and
    // glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING) force a CPU->GPU
    // sync on many drivers and were costing ~15ms/frame on this GPU.
    // We always restore to the engine's default FBO (0) and the
    // window viewport, both already cached.
    glBindFramebuffer(GL_FRAMEBUFFER, sDepthFBO);
    glViewport(0, 0, kShadowMapSize, kShadowMapSize);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Cull front faces during shadow rendering to reduce self-shadow
    // acne on back-facing geometry. Bias is still needed but this
    // pre-filter halves the surface that needs biasing.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void endDepthPass(int restore_w, int restore_h)
{
    glCullFace(GL_BACK);
    // Restore to engine default FBO (always 0 for our setup - we don't
    // use any other render targets). Avoids the per-frame glGetIntegerv
    // round-trip from beginDepthPass.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, restore_w, restore_h);
}

void bindShadowTexture(int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, sDepthTex);
    glActiveTexture(GL_TEXTURE0);
}

const glm::mat4& lightViewProj()
{
    return sLightViewProj;
}

int shadowMapResolution()
{
    return kShadowMapSize;
}

std::uint32_t shadowDepthTexture()
{
    return sDepthTex;
}

void useTerrainDepthShader()
{
    glUseProgram(sTerrainDepthProgram);
    glUniformMatrix4fv(sTerrainDepthLVP, 1, GL_FALSE, glm::value_ptr(sLightViewProj));
}

void setTerrainDepthDiscardRects(const std::vector<glm::vec4>& rects)
{
    const int total = static_cast<int>(rects.size());
    const int n = std::min(total, kMaxDepthDiscards);
    if (total > kMaxDepthDiscards && !sDepthDiscardOverflowWarned)
    {
        std::fprintf(stderr,
                     "[ShadowPass] terrain-depth discard rect count %d exceeds "
                     "MAX_DISCARDS=%d; extras dropped.\n",
                     total, kMaxDepthDiscards);
        sDepthDiscardOverflowWarned = true;
    }
    if (n > 0)
        glUniform4fv(sTerrainDepthDiscardRectsLoc, n, glm::value_ptr(rects[0]));
    glUniform1i(sTerrainDepthDiscardCountLoc, n);
}

void useSceneDepthShader()
{
    glUseProgram(sSceneDepthProgram);
    glUniformMatrix4fv(sSceneDepthLVP, 1, GL_FALSE, glm::value_ptr(sLightViewProj));
}

void useTreeDepthShader()
{
    glUseProgram(sTreeDepthProgram);
    glUniformMatrix4fv(sTreeDepthLVP, 1, GL_FALSE, glm::value_ptr(sLightViewProj));
    glUniform1i(sTreeDepthBaseColor, 0);
}

void useSkeletalDepthShader()
{
    glUseProgram(sSkeletalDepthProgram);
    glUniformMatrix4fv(sSkeletalDepthLVP, 1, GL_FALSE, glm::value_ptr(sLightViewProj));
}

void setSceneDepthModel(const glm::mat4& model)
{
    glUniformMatrix4fv(sSceneDepthModel, 1, GL_FALSE, glm::value_ptr(model));
}

void setTreeDepthModel(const glm::mat4& model)
{
    glUniformMatrix4fv(sTreeDepthModel, 1, GL_FALSE, glm::value_ptr(model));
}

void setTreeDepthWind(float time, float wind_phase)
{
    glUniform1f(sTreeDepthTime, time);
    glUniform1f(sTreeDepthWindPhase, wind_phase);
}

void setTreeDepthAlphaCutoff(float cutoff)
{
    glUniform1f(sTreeDepthAlphaCutoff, cutoff);
}

void setSkeletalDepthModel(const glm::mat4& model)
{
    glUniformMatrix4fv(sSkeletalDepthModel, 1, GL_FALSE, glm::value_ptr(model));
}

void setSkeletalDepthBones(const glm::mat4* bone_palette, int count)
{
    const int clamped = (count > 128) ? 128 : count;
    glUniformMatrix4fv(sSkeletalDepthBones, clamped, GL_FALSE, glm::value_ptr(bone_palette[0]));
}

} // namespace selva::render
