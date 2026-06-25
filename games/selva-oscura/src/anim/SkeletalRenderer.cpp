#include "anim/SkeletalRenderer.h"

#include "anim/SkeletalMesh.h"
#include "gl/ShaderUtils.h"
#include "render/ShadowShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>

namespace selva::anim
{

namespace
{

// ---------------------------------------------------------------------------
// Shader source
//
// Vertex shader: the conceptual heart of skeletal animation.
//   For each vertex, look up the four bones it's influenced by in the bone
//   palette, build a weighted skin matrix, and transform the rest-pose
//   position into the current pose.
//
//   skinMatrix = w0 * bones[i0] + w1 * bones[i1] + w2 * bones[i2] + w3 * bones[i3]
//   worldPos   = uModel * skinMatrix * vec4(aPos, 1.0)
//
//   The rest-pose vertex positions in the VBO are in *local mesh space*.
//   The bone palette is in *model space* (root-to-bone cumulative). The
//   model matrix puts model space into world space. Multiply in that order.
//
//   Normals get the same skin matrix, but only its rotation/scale part —
//   we strip translation by promoting to a vec4 with w=0.0.
// ---------------------------------------------------------------------------

// Bone palette lives in an SSBO -- no compile-time cap. Rigs of any
// size upload as one buffer-orphan per draw. Binding=0 matches the
// glBindBufferBase call in drawSkeletalMesh.
constexpr GLuint kBonePaletteBinding = 0;

const char* kVertexShader = R"glsl(
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uViewProj;

layout(std430, binding = 0) readonly buffer BonePalette {
    mat4 uBones[];
};

out vec3 vNormalWorld;
out vec3 vWorldPos;
out vec2 vUV;

void main()
{
    mat4 skinMatrix =
        aBoneWeights.x * uBones[aBoneIndices.x] +
        aBoneWeights.y * uBones[aBoneIndices.y] +
        aBoneWeights.z * uBones[aBoneIndices.z] +
        aBoneWeights.w * uBones[aBoneIndices.w];

    vec4 localPos = skinMatrix * vec4(aPos, 1.0);
    vec4 worldPos4 = uModel * localPos;
    gl_Position = uViewProj * worldPos4;
    vWorldPos = worldPos4.xyz;

    vec4 localNormal = skinMatrix * vec4(aNormal, 0.0);
    vNormalWorld = normalize(mat3(uModel) * vec3(localNormal));

    vUV = aUV;
}
)glsl";

// Fragment shader: half-Lambert lit by the centralized sun, attenuated
// by the shadow map. Reads vWorldPos from the VS so the shadow sample
// can compare against the depth FBO. Output is uTint-modulated (player
// is white, enemies are bordeaux red) so silhouettes stay distinct.
const char* kFragmentShaderCore = R"glsl(
#version 430 core
in vec3 vNormalWorld;
in vec3 vWorldPos;
in vec2 vUV;

uniform vec3 uTint;
uniform vec3 uSunDir;
uniform float uAlpha;

out vec4 fragColor;
)glsl";

const char* kFragmentShaderMain = R"glsl(
void main()
{
    vec3 N = normalize(vNormalWorld);
    vec3 L = normalize(uSunDir);
    float halfL = dot(N, L) * 0.5 + 0.5;
    float shadow = sampleSunShadow(vWorldPos, N);
    float lit = 0.30 + 0.70 * halfL * shadow;
    vec3 c = clamp(lit * uTint, 0.0, 1.0);
    fragColor = vec4(c, uAlpha);
}
)glsl";

// Module-state for the program + cached uniform locations. One program
// shared across all skinned draws.
GLuint sProgram = 0;
GLuint sBoneSsbo = 0; // shader-storage buffer holding the bone palette
                      // for the current draw. Reused across all draws --
                      // glBufferData with the per-actor palette per call.
GLint sUniModel = -1;
GLint sUniViewProj = -1;
GLint sUniTint = -1;
GLint sUniSunDir = -1;
GLint sUniAlpha = -1;
GLint sUniShadowMap = -1;
GLint sUniLightViewProj = -1;
GLint sUniShadowSunDir = -1;
GLint sUniShadowCamPos = -1;

glm::vec3 sFrameSunDir(0.0f, 1.0f, 0.0f);
glm::mat4 sFrameLightVP(1.0f);
glm::vec3 sFrameShadowCamPos(0.0f);
int sFrameShadowUnit = 1;

} // namespace

bool initSkeletalRenderer()
{
    if (sProgram != 0)
        return true; // idempotent

    using namespace selva::render;
    const std::string fs = std::string(kFragmentShaderCore) + kShadowGLSL + kFragmentShaderMain;
    sProgram = engine::gl::compileProgram(kVertexShader, fs.c_str());
    if (sProgram == 0)
    {
        std::fprintf(stderr, "[SkeletalRenderer] shader compile/link failed\n");
        return false;
    }
    sUniModel = glGetUniformLocation(sProgram, "uModel");
    sUniViewProj = glGetUniformLocation(sProgram, "uViewProj");
    sUniTint = glGetUniformLocation(sProgram, "uTint");
    sUniSunDir = glGetUniformLocation(sProgram, "uSunDir");
    sUniAlpha = glGetUniformLocation(sProgram, "uAlpha");
    sUniShadowMap = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProj = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDir = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPos = glGetUniformLocation(sProgram, "uShadowCameraPos");

    glGenBuffers(1, &sBoneSsbo);
    return true;
}

void setSkeletalSun(const glm::vec3& sun_dir)
{
    sFrameSunDir = sun_dir;
}

void setSkeletalShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                       const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    sFrameLightVP = light_view_proj;
    sFrameShadowUnit = shadow_texture_unit;
    sFrameSunDir = sun_dir;
    sFrameShadowCamPos = shadow_cam_pos;
}

void shutdownSkeletalRenderer()
{
    if (sBoneSsbo != 0)
    {
        glDeleteBuffers(1, &sBoneSsbo);
        sBoneSsbo = 0;
    }
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
}

void beginSkeletalPass()
{
    if (sProgram == 0)
        return;
    glUseProgram(sProgram);
    // Skeletal meshes from third-party sources can't be assumed to
    // have consistently outward-wound normals -- back-face culling
    // makes parts of those rigs see-through at some angles. Disable
    // culling for the whole pass; correctness > fill rate. Static
    // meshes get culling back at endSkeletalPass.
    glDisable(GL_CULL_FACE);
}

void endSkeletalPass()
{
    // Restore the state the rest of the frame expects: cull-back ON,
    // blend OFF, depth-mask ON, no program bound.
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint,
                      float alpha)
{
    if (sProgram == 0 || !mesh.isLoaded())
        return;
    if (alpha <= 0.001f)
        return;

    glUniformMatrix4fv(sUniModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(sUniViewProj, 1, GL_FALSE, glm::value_ptr(view_proj));
    glUniform3fv(sUniTint, 1, glm::value_ptr(tint));
    glUniform3fv(sUniSunDir, 1, glm::value_ptr(sFrameSunDir));
    glUniform1f(sUniAlpha, alpha);
    glUniformMatrix4fv(sUniLightViewProj, 1, GL_FALSE, glm::value_ptr(sFrameLightVP));
    glUniform3fv(sUniShadowSunDir, 1, glm::value_ptr(sFrameSunDir));
    glUniform3fv(sUniShadowCamPos, 1, glm::value_ptr(sFrameShadowCamPos));
    glUniform1i(sUniShadowMap, sFrameShadowUnit);

    // Upload the bone palette to the SSBO. Orphan + refill each draw
    // (NULL data + new data) lets the driver allocate fresh storage
    // without stalling on in-flight draws using the previous palette.
    if (!bone_palette.empty())
    {
        const GLsizeiptr bytes =
            static_cast<GLsizeiptr>(bone_palette.size() * sizeof(glm::mat4));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sBoneSsbo);
        glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, glm::value_ptr(bone_palette[0]),
                     GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kBonePaletteBinding, sBoneSsbo);
    }

    // Per-draw blend state. Opaque draws keep depth-write on so they
    // occlude correctly; transparent draws disable depth-write so
    // overlapping fading corpses don't z-fight. Caller must order
    // opaque-before-transparent and sort transparent back-to-front.
    const bool blend_needed = (alpha < 0.999f);
    if (blend_needed)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace selva::anim
