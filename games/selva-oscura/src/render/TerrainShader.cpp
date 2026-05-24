#include "render/TerrainShader.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"
#include "render/ShadowShader.h"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <string>

#include <glad/glad.h>

namespace selva::render
{

namespace
{

// Vertices already in world space (Terrain.cpp bakes world positions).
const char* kTerrainVS = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vWorldPos = aPos;
    vNormal = normalize(aNormal);
    vUV = aUV;
}
)glsl";

const char* kTerrainFSCore = R"glsl(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
out vec4 fragColor;

uniform vec3 uBaseColor;
uniform vec3 uDarkLoam;
uniform vec3 uDryDirt;
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;

// Axis-aligned XZ rectangle for terrain excision (chapel body
// footprint). Fragments inside are discarded so the chapel mesh
// provides the ground there without a competing terrain surface.
// Set half-extents to (0, 0) to disable the rect.
uniform vec2 uChapelDiscardCenter;
uniform vec2 uChapelDiscardHalfExtents;
// Half-disc (apse bulge) for terrain excision behind the chapel.
// Center at the chapel back wall midpoint; only discards fragments
// with z <= center.z (the apse-side half). Set radius to 0 to
// disable.
uniform vec2 uApseDiscardCenter;
uniform float uApseDiscardRadius;
// Descent shaft rect for terrain excision under the underground
// descent path (landing + corridor + Acheron stub). Without it,
// terrain renders over the descent area outside the chapel/apse
// footprints. Set half-extents to (0, 0) to disable.
uniform vec2 uDescentDiscardCenter;
uniform vec2 uDescentDiscardHalfExtents;
)glsl";

const char* kTerrainFSMain = R"glsl(
// Cheap hash-based 3D noise. No texture upload; uses fract(sin(dot(.)))
// pattern, two octaves for coarse + fine detail.
float hash13(vec3 p)
{
    p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(
        mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
        mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y),
        f.z);
}

void main()
{
    // Chapel-body excision: rectangular footprint.
    if (uChapelDiscardHalfExtents.x > 0.0 && uChapelDiscardHalfExtents.y > 0.0)
    {
        vec2 d = abs(vWorldPos.xz - uChapelDiscardCenter);
        if (d.x < uChapelDiscardHalfExtents.x && d.y < uChapelDiscardHalfExtents.y)
            discard;
    }
    // Apse excision: half-disc behind the chapel body. Disc center
    // at the chapel back wall midpoint; discard fragments with
    // z <= center.z AND inside the radius. Avoids the rectangular
    // void that would appear at the apse-bulge corners if we
    // used a single rect that covered the apse Z range.
    if (uApseDiscardRadius > 0.0)
    {
        vec2 ad = vWorldPos.xz - uApseDiscardCenter;
        if (ad.y <= 0.0 && dot(ad, ad) < uApseDiscardRadius * uApseDiscardRadius)
            discard;
    }
    // Descent shaft excision: rectangle covering the underground
    // landing + corridor + Acheron stub.
    if (uDescentDiscardHalfExtents.x > 0.0 && uDescentDiscardHalfExtents.y > 0.0)
    {
        vec2 d = abs(vWorldPos.xz - uDescentDiscardCenter);
        if (d.x < uDescentDiscardHalfExtents.x && d.y < uDescentDiscardHalfExtents.y)
            discard;
    }

    // Two-octave noise sampled at world XZ (Y omitted so the texture
    // doesn't smear vertically up cliffs). Coarse 5m features + fine
    // 0.7m mottling, weighted 0.7 / 0.3.
    float n_coarse = vnoise(vec3(vWorldPos.x * 0.2, 0.0, vWorldPos.z * 0.2));
    float n_fine = vnoise(vec3(vWorldPos.x * 1.4, 0.0, vWorldPos.z * 1.4));
    float n = mix(n_coarse, n_fine, 0.3);

    // Slope (0 flat, 1 vertical) — slopes show drier exposed dirt.
    float slope = clamp(1.0 - vNormal.y, 0.0, 1.0);
    float dryness = clamp(slope * 0.7 + (n - 0.5) * 0.6 + 0.15, 0.0, 1.0);
    vec3 dirt = mix(uDarkLoam, uDryDirt, dryness);

    // Sun lighting (half-Lambert + ambient + sun tint), attenuated
    // by the directional-light shadow map sample.
    float halfL = dot(vNormal, uSunDir) * 0.5 + 0.5;
    vec3 ambient = vec3(0.15, 0.18, 0.24);
    vec3 sunTint = vec3(1.05, 0.78, 0.55);
    float shadow = sampleSunShadow(vWorldPos, vNormal);
    vec3 surface = dirt * (ambient + sunTint * halfL * shadow);

    vec3 viewVec = vWorldPos - uCamPos;
    float dist = length(viewVec);
    vec3 rayDir = viewVec / max(dist, 1e-4);
    vec3 transmittance;
    vec3 inScatter = atmosphereWithT(uCamPos, rayDir, uSunDir, uSunIntensity,
                                     dist, transmittance);

    vec3 col = surface * transmittance + inScatter;
    col = applyDistanceFog(col, rayDir, uSunDir, dist);
    col = col * uExposure;
    col = col / (col + vec3(1.0));
    fragColor = vec4(col, 1.0);
}
)glsl";

GLuint sProgram = 0;
GLint sUniViewProjLoc = -1;
GLint sUniBaseColorLoc = -1;
GLint sUniDarkLoamLoc = -1;
GLint sUniDryDirtLoc = -1;
GLint sUniSunDirLoc = -1;
GLint sUniSunIntensityLoc = -1;
GLint sUniCamPosLoc = -1;
GLint sUniExposureLoc = -1;
GLint sUniShadowMapLoc = -1;
GLint sUniLightViewProjLoc = -1;
GLint sUniShadowSunDirLoc = -1;
GLint sUniShadowCamPosLoc = -1;
GLint sUniChapelDiscardCenterLoc = -1;
GLint sUniChapelDiscardHalfExtentsLoc = -1;
GLint sUniApseDiscardCenterLoc = -1;
GLint sUniApseDiscardRadiusLoc = -1;
GLint sUniDescentDiscardCenterLoc = -1;
GLint sUniDescentDiscardHalfExtentsLoc = -1;

// Cached last-set discard values so the collider debug overlay can
// draw exactly what the terrain shader is using right now.
glm::vec2 sLastChapelDiscardCenter{0.0f};
glm::vec2 sLastChapelDiscardHalfExtents{0.0f};
glm::vec2 sLastApseDiscardCenter{0.0f};
float sLastApseDiscardRadius = 0.0f;
glm::vec2 sLastDescentDiscardCenter{0.0f};
glm::vec2 sLastDescentDiscardHalfExtents{0.0f};

} // namespace

bool initTerrainShader()
{
    const std::string fs = std::string(kTerrainFSCore) + kAtmosphereGLSL + kShadowGLSL +
                           kTerrainFSMain;
    sProgram = engine::gl::compileProgram(kTerrainVS, fs.c_str());
    if (sProgram == 0)
        return false;
    sUniViewProjLoc = glGetUniformLocation(sProgram, "uViewProj");
    sUniBaseColorLoc = glGetUniformLocation(sProgram, "uBaseColor");
    sUniDarkLoamLoc = glGetUniformLocation(sProgram, "uDarkLoam");
    sUniDryDirtLoc = glGetUniformLocation(sProgram, "uDryDirt");
    sUniSunDirLoc = glGetUniformLocation(sProgram, "uSunDir");
    sUniSunIntensityLoc = glGetUniformLocation(sProgram, "uSunIntensity");
    sUniCamPosLoc = glGetUniformLocation(sProgram, "uCamPos");
    sUniExposureLoc = glGetUniformLocation(sProgram, "uExposure");
    sUniShadowMapLoc = glGetUniformLocation(sProgram, "uShadowMap");
    sUniLightViewProjLoc = glGetUniformLocation(sProgram, "uLightViewProj");
    sUniShadowSunDirLoc = glGetUniformLocation(sProgram, "uShadowSunDir");
    sUniShadowCamPosLoc = glGetUniformLocation(sProgram, "uShadowCameraPos");
    sUniChapelDiscardCenterLoc = glGetUniformLocation(sProgram, "uChapelDiscardCenter");
    sUniChapelDiscardHalfExtentsLoc = glGetUniformLocation(sProgram, "uChapelDiscardHalfExtents");
    sUniApseDiscardCenterLoc = glGetUniformLocation(sProgram, "uApseDiscardCenter");
    sUniApseDiscardRadiusLoc = glGetUniformLocation(sProgram, "uApseDiscardRadius");
    sUniDescentDiscardCenterLoc = glGetUniformLocation(sProgram, "uDescentDiscardCenter");
    sUniDescentDiscardHalfExtentsLoc = glGetUniformLocation(sProgram, "uDescentDiscardHalfExtents");
    return true;
}

void shutdownTerrainShader()
{
    if (sProgram != 0)
    {
        glDeleteProgram(sProgram);
        sProgram = 0;
    }
    sUniViewProjLoc = sUniBaseColorLoc = sUniDarkLoamLoc = sUniDryDirtLoc = sUniSunDirLoc =
        sUniSunIntensityLoc = sUniCamPosLoc = sUniExposureLoc = -1;
}

void useTerrainShader()
{
    glUseProgram(sProgram);
}

void setTerrainViewProj(const glm::mat4& view_proj)
{
    glUniformMatrix4fv(sUniViewProjLoc, 1, GL_FALSE, glm::value_ptr(view_proj));
}

void setTerrainAtmosphere(const glm::vec3& sun_dir, const glm::vec3& sun_intensity,
                          const glm::vec3& cam_pos, float exposure)
{
    const float len =
        std::sqrt(sun_dir.x * sun_dir.x + sun_dir.y * sun_dir.y + sun_dir.z * sun_dir.z);
    const float inv = (len > 1e-6f) ? (1.0f / len) : 1.0f;
    glUniform3f(sUniSunDirLoc, sun_dir.x * inv, sun_dir.y * inv, sun_dir.z * inv);
    glUniform3f(sUniSunIntensityLoc, sun_intensity.x, sun_intensity.y, sun_intensity.z);
    glUniform3f(sUniCamPosLoc, cam_pos.x, cam_pos.y, cam_pos.z);
    glUniform1f(sUniExposureLoc, exposure);
}

void setTerrainBaseColor(const glm::vec3& color)
{
    glUniform3f(sUniBaseColorLoc, color.x, color.y, color.z);
}

void setTerrainTones(const glm::vec3& dark_loam, const glm::vec3& dry_dirt)
{
    glUniform3f(sUniDarkLoamLoc, dark_loam.x, dark_loam.y, dark_loam.z);
    glUniform3f(sUniDryDirtLoc, dry_dirt.x, dry_dirt.y, dry_dirt.z);
}

void setTerrainShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                      const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    glUniformMatrix4fv(sUniLightViewProjLoc, 1, GL_FALSE, glm::value_ptr(light_view_proj));
    glUniform3f(sUniShadowSunDirLoc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform3f(sUniShadowCamPosLoc, shadow_cam_pos.x, shadow_cam_pos.y, shadow_cam_pos.z);
    glUniform1i(sUniShadowMapLoc, shadow_texture_unit);
}

void setTerrainChapelDiscard(const glm::vec2& center, const glm::vec2& half_extents)
{
    glUniform2f(sUniChapelDiscardCenterLoc, center.x, center.y);
    glUniform2f(sUniChapelDiscardHalfExtentsLoc, half_extents.x, half_extents.y);
    sLastChapelDiscardCenter = center;
    sLastChapelDiscardHalfExtents = half_extents;
}

void setTerrainApseDiscard(const glm::vec2& center, float radius)
{
    glUniform2f(sUniApseDiscardCenterLoc, center.x, center.y);
    glUniform1f(sUniApseDiscardRadiusLoc, radius);
    sLastApseDiscardCenter = center;
    sLastApseDiscardRadius = radius;
}

void setTerrainDescentDiscard(const glm::vec2& center, const glm::vec2& half_extents)
{
    glUniform2f(sUniDescentDiscardCenterLoc, center.x, center.y);
    glUniform2f(sUniDescentDiscardHalfExtentsLoc, half_extents.x, half_extents.y);
    sLastDescentDiscardCenter = center;
    sLastDescentDiscardHalfExtents = half_extents;
}

glm::vec2 lastTerrainChapelDiscardCenter() { return sLastChapelDiscardCenter; }
glm::vec2 lastTerrainChapelDiscardHalfExtents() { return sLastChapelDiscardHalfExtents; }
glm::vec2 lastTerrainApseDiscardCenter() { return sLastApseDiscardCenter; }
float     lastTerrainApseDiscardRadius() { return sLastApseDiscardRadius; }
glm::vec2 lastTerrainDescentDiscardCenter() { return sLastDescentDiscardCenter; }
glm::vec2 lastTerrainDescentDiscardHalfExtents() { return sLastDescentDiscardHalfExtents; }

} // namespace selva::render
