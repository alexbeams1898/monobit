#include "world/Lights.h"

#include <cmath>

namespace engine::world
{

namespace
{
std::vector<LightSource> sLights;
}

void registerLight(const LightSource& light)
{
    sLights.push_back(light);
}

void clearLights()
{
    sLights.clear();
}

int lightCount()
{
    return static_cast<int>(sLights.size());
}

const LightSource& lightAt(int idx)
{
    return sLights[idx];
}

const std::vector<LightSource>& allLights()
{
    return sLights;
}

float flickerIntensity(int light_index, float time_seconds)
{
    if (light_index < 0 || light_index >= static_cast<int>(sLights.size()))
        return 0.0f;
    const LightSource& L = sLights[static_cast<size_t>(light_index)];
    if (L.flicker_amp <= 0.0f || L.flicker_freq <= 0.0f)
        return L.intensity;
    // Per-light phase derived from index — keeps neighboring lights
    // out of sync without needing to store a phase field per light.
    // Multiply by an irrational so the phases don't repeat at low
    // light counts (1.6180... ≈ golden ratio).
    constexpr float k2Pi = 6.28318530718f;
    const float phase = static_cast<float>(light_index) * 1.6180339887f * k2Pi;
    const float t = time_seconds * L.flicker_freq * k2Pi + phase;
    const float mod = 1.0f + L.flicker_amp * std::sin(t);
    return L.intensity * mod;
}

} // namespace engine::world
