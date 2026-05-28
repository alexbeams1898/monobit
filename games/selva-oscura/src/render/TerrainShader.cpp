#include "render/TerrainShader.h"

#include "gl/ShaderUtils.h"
#include "render/AtmosphereShader.h"
#include "render/ShadowShader.h"
#include "world/Lights.h"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
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
// Per-region lighting environment (set by WorldRenderer's terrain
// loop). uSunMultiplier scales direct-sun contribution to 0 for
// underground regions where no sun reaches. uSkyAmbient + uGroundAmbient
// drive the hemispheric ambient blend so each region's "above" and
// "below" indirect light read appropriately (Selva surface: bluish sky
// + warm dirt; Limbo: dim cavern).
uniform float uSunMultiplier;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform vec3 uSunDir;
uniform vec3 uSunIntensity;
uniform vec3 uCamPos;
uniform float uExposure;

// Axis-aligned XZ rectangles for terrain excision. Populated per
// region from registered StructureFootprints with cuts_floor=true —
// fragments inside any rect are discarded so the structure mesh
// provides the ground there without a competing terrain surface.
//
// Layout: uDiscardRects[i].xy = center XZ, uDiscardRects[i].zw =
// half-extents XZ. Empty (uDiscardCount=0) disables all discards.
// MAX_DISCARDS matches StructureFootprint runtime cap; raise both
// together if any region ever needs more than this many cuts.
#define MAX_DISCARDS 16
uniform vec4 uDiscardRects[MAX_DISCARDS];
uniform int uDiscardCount;

// Point lights for the active region. Packed as:
//   uLightPosRadius.xyz = world position
//   uLightPosRadius.w   = radius (meters; brightness 0 past this)
//   uLightColorIntensity.rgb = linear RGB color
//   uLightColorIntensity.w   = intensity scalar
// Variable count via uLightCount; lights beyond MAX_LIGHTS are dropped
// at upload time (logged). 64 chosen to keep the uniform array under
// most drivers' default vec4 cap without needing a UBO.
#define MAX_LIGHTS 64
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform int uLightCount;
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
    // Structure-footprint excision: discard fragments inside any
    // registered cuts_floor rect for this region. WorldRenderer
    // uploads the rect list once per region before the draw.
    for (int i = 0; i < uDiscardCount; ++i)
    {
        vec2 center = uDiscardRects[i].xy;
        vec2 half_ext = uDiscardRects[i].zw;
        vec2 d = abs(vWorldPos.xz - center);
        if (d.x < half_ext.x && d.y < half_ext.y)
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

    // Sun lighting (half-Lambert + hemispheric ambient + sun tint),
    // attenuated by the directional-light shadow map sample.
    float halfL = dot(vNormal, uSunDir) * 0.5 + 0.5;
    float skyFactor = dot(vNormal, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5;
    vec3 ambient = mix(uGroundAmbient, uSkyAmbient, skyFactor);
    vec3 sunTint = vec3(1.05, 0.78, 0.55) * uSunMultiplier;
    float shadow = sampleSunShadow(vWorldPos, vNormal);

    // Point-light contribution. Smooth falloff from full at distance 0
    // to zero at radius; half-Lambert against the light direction so
    // back-facing surfaces still pick up a faint wash (cheap stand-in
    // for indirect bounce). No shadow-cast in v1.
    vec3 pointLight = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i)
    {
        vec3 toLight = uLightPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = uLightPosRadius[i].w;
        if (radius <= 0.0 || dist >= radius)
            continue;
        vec3 ldir = toLight / max(dist, 1e-4);
        float ndotl = dot(vNormal, ldir) * 0.5 + 0.5;
        float falloff = 1.0 - smoothstep(0.0, radius, dist);
        pointLight += uLightColorIntensity[i].rgb * uLightColorIntensity[i].w
                      * (ndotl * falloff);
    }

    vec3 surface = dirt * (ambient + sunTint * halfL * shadow + pointLight);

    vec3 viewVec = vWorldPos - uCamPos;
    float dist = length(viewVec);
    vec3 rayDir = viewVec / max(dist, 1e-4);
    vec3 transmittance;
    vec3 inScatter = atmosphereWithT(uCamPos, rayDir, uSunDir, uSunIntensity,
                                     dist, transmittance);
    // Atmospheric in-scatter requires sun light to scatter; in regions
    // with no sun reaching (underground) the in-scatter and the
    // transmittance attenuation are both inappropriate. Gate both on
    // the region's sun multiplier.
    inScatter *= uSunMultiplier;
    transmittance = mix(vec3(1.0), transmittance, uSunMultiplier);

    vec3 col = surface * transmittance + inScatter;
    vec3 foggy = applyDistanceFog(col, rayDir, uSunDir, dist);
    col = mix(col, foggy, uSunMultiplier);
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
GLint sUniDiscardRectsLoc = -1;
GLint sUniDiscardCountLoc = -1;
GLint sUniSunMultiplierLoc = -1;
GLint sUniSkyAmbientLoc = -1;
GLint sUniGroundAmbientLoc = -1;
GLint sUniLightPosRadiusLoc = -1;
GLint sUniLightColorIntensityLoc = -1;
GLint sUniLightCountLoc = -1;

// Must match the MAX_LIGHTS define in the FS uniform block.
constexpr int kMaxLights = 64;
bool sLightOverflowWarned = false;

// Must match the MAX_DISCARDS define in the FS uniform block.
constexpr int kMaxDiscards = 16;
bool sDiscardOverflowWarned = false;

} // namespace

bool initTerrainShader()
{
    const std::string fs =
        std::string(kTerrainFSCore) + kAtmosphereGLSL + kShadowGLSL + kTerrainFSMain;
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
    sUniDiscardRectsLoc = glGetUniformLocation(sProgram, "uDiscardRects");
    sUniDiscardCountLoc = glGetUniformLocation(sProgram, "uDiscardCount");
    sUniSunMultiplierLoc = glGetUniformLocation(sProgram, "uSunMultiplier");
    sUniSkyAmbientLoc = glGetUniformLocation(sProgram, "uSkyAmbient");
    sUniGroundAmbientLoc = glGetUniformLocation(sProgram, "uGroundAmbient");
    sUniLightPosRadiusLoc = glGetUniformLocation(sProgram, "uLightPosRadius");
    sUniLightColorIntensityLoc = glGetUniformLocation(sProgram, "uLightColorIntensity");
    sUniLightCountLoc = glGetUniformLocation(sProgram, "uLightCount");
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

void setTerrainLightingEnv(float sun_multiplier, const glm::vec3& sky_ambient,
                           const glm::vec3& ground_ambient)
{
    glUniform1f(sUniSunMultiplierLoc, sun_multiplier);
    glUniform3f(sUniSkyAmbientLoc, sky_ambient.x, sky_ambient.y, sky_ambient.z);
    glUniform3f(sUniGroundAmbientLoc, ground_ambient.x, ground_ambient.y, ground_ambient.z);
}

void setTerrainPointLights(const std::vector<engine::world::LightSource>& lights)
{
    const int total = static_cast<int>(lights.size());
    const int n = std::min(total, kMaxLights);
    if (total > kMaxLights && !sLightOverflowWarned)
    {
        std::fprintf(stderr,
                     "[TerrainShader] light count %d exceeds MAX_LIGHTS=%d; "
                     "extras dropped. Raise the cap or filter by region.\n",
                     total, kMaxLights);
        sLightOverflowWarned = true;
    }

    // Pack into transient stack buffers; one glUniform4fv per array.
    float pos_radius[kMaxLights * 4];
    float color_intensity[kMaxLights * 4];
    for (int i = 0; i < n; ++i)
    {
        const auto& L = lights[static_cast<size_t>(i)];
        pos_radius[i * 4 + 0] = L.position.x;
        pos_radius[i * 4 + 1] = L.position.y;
        pos_radius[i * 4 + 2] = L.position.z;
        pos_radius[i * 4 + 3] = L.radius;
        color_intensity[i * 4 + 0] = L.color.x;
        color_intensity[i * 4 + 1] = L.color.y;
        color_intensity[i * 4 + 2] = L.color.z;
        color_intensity[i * 4 + 3] = L.intensity;
    }
    if (n > 0)
    {
        glUniform4fv(sUniLightPosRadiusLoc, n, pos_radius);
        glUniform4fv(sUniLightColorIntensityLoc, n, color_intensity);
    }
    glUniform1i(sUniLightCountLoc, n);
}

void setTerrainShadow(const glm::mat4& light_view_proj, const glm::vec3& sun_dir,
                      const glm::vec3& shadow_cam_pos, int shadow_texture_unit)
{
    glUniformMatrix4fv(sUniLightViewProjLoc, 1, GL_FALSE, glm::value_ptr(light_view_proj));
    glUniform3f(sUniShadowSunDirLoc, sun_dir.x, sun_dir.y, sun_dir.z);
    glUniform3f(sUniShadowCamPosLoc, shadow_cam_pos.x, shadow_cam_pos.y, shadow_cam_pos.z);
    glUniform1i(sUniShadowMapLoc, shadow_texture_unit);
}

void setTerrainDiscardRects(const std::vector<glm::vec4>& rects)
{
    const int total = static_cast<int>(rects.size());
    const int n = std::min(total, kMaxDiscards);
    if (total > kMaxDiscards && !sDiscardOverflowWarned)
    {
        std::fprintf(stderr,
                     "[TerrainShader] discard rect count %d exceeds MAX_DISCARDS=%d; "
                     "extras dropped. Raise the cap or split the region.\n",
                     total, kMaxDiscards);
        sDiscardOverflowWarned = true;
    }
    if (n > 0)
        glUniform4fv(sUniDiscardRectsLoc, n, glm::value_ptr(rects[0]));
    glUniform1i(sUniDiscardCountLoc, n);
}

} // namespace selva::render
