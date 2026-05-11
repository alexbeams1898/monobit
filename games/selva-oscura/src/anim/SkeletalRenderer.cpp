#include "anim/SkeletalRenderer.h"

#include "anim/SkeletalMesh.h"
#include "gl/ShaderUtils.h"

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

constexpr int kMaxBones = 128;

const char* kVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uBones[128]; // matches kMaxBones in C++

out vec3 vNormalWorld;
out vec2 vUV;

void main()
{
    mat4 skinMatrix =
        aBoneWeights.x * uBones[aBoneIndices.x] +
        aBoneWeights.y * uBones[aBoneIndices.y] +
        aBoneWeights.z * uBones[aBoneIndices.z] +
        aBoneWeights.w * uBones[aBoneIndices.w];

    vec4 localPos = skinMatrix * vec4(aPos, 1.0);
    gl_Position = uViewProj * uModel * localPos;

    // Normal: skin and rotate to world space. Drop translation by
    // promoting to vec4 with w=0. For non-uniform scaled rigs you'd use
    // the inverse-transpose; uniform-scaled (our case) just works.
    vec4 localNormal = skinMatrix * vec4(aNormal, 0.0);
    vNormalWorld = normalize(mat3(uModel) * vec3(localNormal));

    vUV = aUV;
}
)glsl";

// Fragment shader: simple Lambert lighting against a hardcoded directional
// light (sun coming from above and slightly forward). Output is grayscale
// so it matches the existing scene shader's visual register; the tumbling
// scene cube and the soldier will read as the same world. Real lighting +
// the 1-bit dither pass replaces this when the visual identity milestone
// lands — this is the "looks 3D enough to validate the skinning works" stage.
const char* kFragmentShader = R"glsl(
#version 330 core
in vec3 vNormalWorld;
in vec2 vUV;

uniform vec3 uTint;

out vec4 fragColor;

void main()
{
    // Hardcoded sun: slightly above + in front of camera (camera looks
    // down -Z, so positive Z = behind). Direction points TOWARD the light.
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.6));
    float ndotl = max(dot(normalize(vNormalWorld), lightDir), 0.0);

    // Wrap a small ambient term so unlit faces aren't pitch black. 0.25
    // ambient + 0.75 diffuse keeps the silhouette readable from any angle.
    float lambert = 0.25 + 0.75 * ndotl;

    vec3 c = clamp(lambert * uTint, 0.0, 1.0);
    fragColor = vec4(c, 1.0);
}
)glsl";

// Module-state for the program + cached uniform locations. One program
// shared across all skinned draws.
GLuint sProgram = 0;
GLint sUniModel = -1;
GLint sUniViewProj = -1;
GLint sUniBones = -1; // first element of the array; OpenGL exposes the
                      // array as a single base location and you upload
                      // count consecutive matrices via glUniformMatrix4fv.
GLint sUniTint = -1;

} // namespace

bool initSkeletalRenderer()
{
    if (sProgram != 0)
        return true; // idempotent

    sProgram = engine::gl::compileProgram(kVertexShader, kFragmentShader);
    if (sProgram == 0)
    {
        std::fprintf(stderr, "[SkeletalRenderer] shader compile/link failed\n");
        return false;
    }
    sUniModel = glGetUniformLocation(sProgram, "uModel");
    sUniViewProj = glGetUniformLocation(sProgram, "uViewProj");
    sUniBones = glGetUniformLocation(sProgram, "uBones");
    sUniTint = glGetUniformLocation(sProgram, "uTint");
    return true;
}

void shutdownSkeletalRenderer()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
}

void drawSkeletalMesh(const SkeletalMesh& mesh, const glm::mat4& model, const glm::mat4& view_proj,
                      const std::vector<glm::mat4>& bone_palette, const glm::vec3& tint)
{
    if (sProgram == 0 || !mesh.isLoaded())
        return;

    glUseProgram(sProgram);
    glUniformMatrix4fv(sUniModel, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(sUniViewProj, 1, GL_FALSE, glm::value_ptr(view_proj));
    glUniform3fv(sUniTint, 1, glm::value_ptr(tint));

    // Upload the bone palette. Cap at kMaxBones — any rig past that gets
    // truncated. With 49 bones for the soldier we have lots of headroom.
    const GLsizei bone_count =
        static_cast<GLsizei>(bone_palette.size() < kMaxBones ? bone_palette.size() : kMaxBones);
    if (bone_count > 0)
    {
        glUniformMatrix4fv(sUniBones, bone_count, GL_FALSE, glm::value_ptr(bone_palette[0]));
    }

    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glUseProgram(0);
}

} // namespace selva::anim
