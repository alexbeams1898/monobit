#pragma once

namespace selva::render
{

// GLSL source for a shared single-scattering atmospheric model
// (Rayleigh + Mie, ray-marched). Included verbatim by both the sky
// pass shader (infinite view rays) and the scene shader (finite
// view rays for aerial perspective).
//
// Function signature provided:
//   vec3 atmosphere(vec3 rayOrigin, vec3 rayDir, vec3 sunDir,
//                   vec3 sunIntensity, float maxDist);
//
// Returns in-scattered radiance along the ray segment from
// rayOrigin in direction rayDir, clamped to maxDist meters. For
// sky-pass use, pass a very large maxDist (e.g. 1e9). For aerial
// perspective in scene shader, pass distance to the geometry hit.
//
// The function also writes the optical-extinction transmittance to
// a varying named `outTransmittance` if the caller sets it as an
// out parameter — for blending atmosphere over geometry color.
inline const char* kAtmosphereGLSL = R"glsl(
const float kPI = 3.14159265358979;

// Earth-scale atmosphere parameters.
const float kEarthRadius     = 6371000.0;   // meters
const float kAtmosphereTop   = 6471000.0;   // meters (100km thick)
const float kRayleighScale   = 8000.0;      // density scale height
const float kMieScale        = 1200.0;      // density scale height

// Mystical-register scattering, not Earth-accurate. Rayleigh blue
// channel pulled DOWN (less daytime-blue), red bumped slightly so
// the cool-direction reads as deep blue-violet rather than sky-blue.
// Mie kept strong + g pulled tighter so the warm direction has a
// concentrated jewel-tone pool over the colle direction.
const vec3  kBetaRayleigh    = vec3(7.5e-6, 11.0e-6, 16.0e-6);
const vec3  kBetaMie         = vec3(28e-6);
const float kMieG            = 0.85;

// Far hit of ray with sphere centered at origin, radius `radius`.
// Returns -1 if miss.
float raySphereFar(vec3 origin, vec3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    return -b + sqrt(h);
}

// Near positive hit of ray with sphere. Returns -1 if miss or
// behind origin.
float raySphereNear(vec3 origin, vec3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return (t < 0.0) ? -1.0 : t;
}

// Same as atmosphere() but also writes the camera->endpoint
// transmittance to outTransmittance so the caller can blend
// in-scattered light over surface color (aerial perspective).
vec3 atmosphereWithT(vec3 rayOrigin, vec3 rayDir, vec3 sunDir,
                     vec3 sunIntensity, float maxDist, out vec3 outTransmittance);

vec3 atmosphere(vec3 rayOrigin, vec3 rayDir, vec3 sunDir,
                vec3 sunIntensity, float maxDist)
{
    vec3 t;
    return atmosphereWithT(rayOrigin, rayDir, sunDir, sunIntensity, maxDist, t);
}

vec3 atmosphereWithT(vec3 rayOrigin, vec3 rayDir, vec3 sunDir,
                     vec3 sunIntensity, float maxDist, out vec3 outTransmittance)
{
    vec3 origin = vec3(0.0, kEarthRadius, 0.0) + rayOrigin;

    // Integrate atmospheric scattering along the ray. We do NOT clip
    // at the planet surface — for downward sky rays this would make
    // the lower sky read as faked-ground and produce a visible seam
    // against real scene geometry. Instead the per-sample density
    // formula clamps altitude to non-negative so the march remains
    // numerically sane even if a sample is below sea level.
    float tAtm = raySphereFar(origin, rayDir, kAtmosphereTop);
    if (tAtm < 0.0) return vec3(0.0);
    float tMax = min(tAtm, maxDist);

    const int kSamples    = 16;
    const int kLightSteps = 8;
    float segLen = tMax / float(kSamples);

    // Phase functions.
    float mu = dot(rayDir, sunDir);
    float phaseR = (3.0 / (16.0 * kPI)) * (1.0 + mu * mu);
    float g  = kMieG;
    float g2 = g * g;
    float phaseM = (3.0 / (8.0 * kPI)) *
        ((1.0 - g2) * (1.0 + mu * mu)) /
        ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * mu, 1.5));

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;

    for (int i = 0; i < kSamples; ++i)
    {
        vec3 samplePos = origin + rayDir * (float(i) + 0.5) * segLen;
        float h = max(length(samplePos) - kEarthRadius, 0.0);
        float densityR = exp(-h / kRayleighScale) * segLen;
        float densityM = exp(-h / kMieScale) * segLen;
        odR += densityR;
        odM += densityM;

        // Secondary ray toward the sun for transmittance.
        float tLight = raySphereFar(samplePos, sunDir, kAtmosphereTop);
        if (tLight < 0.0) continue;
        float lStep = tLight / float(kLightSteps);
        float odLightR = 0.0;
        float odLightM = 0.0;
        for (int j = 0; j < kLightSteps; ++j)
        {
            vec3 lp = samplePos + sunDir * (float(j) + 0.5) * lStep;
            float lh = max(length(lp) - kEarthRadius, 0.0);
            odLightR += exp(-lh / kRayleighScale) * lStep;
            odLightM += exp(-lh / kMieScale) * lStep;
        }

        vec3 tau = kBetaRayleigh * (odR + odLightR) +
                   kBetaMie * 1.1 * (odM + odLightM);
        vec3 attn = exp(-tau);
        sumR += densityR * attn;
        sumM += densityM * attn;
    }

    vec3 col = sunIntensity *
        (sumR * kBetaRayleigh * phaseR + sumM * kBetaMie * phaseM);

    // Camera->endpoint transmittance (used by scene shader for
    // aerial-perspective blend). exp(-tau) along the marched path.
    vec3 tauView = kBetaRayleigh * odR + kBetaMie * 1.1 * odM;
    outTransmittance = exp(-tauView);

    return col;
}
)glsl";

} // namespace selva::render
