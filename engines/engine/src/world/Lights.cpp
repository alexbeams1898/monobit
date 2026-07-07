#include "world/Lights.h"

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace engine::world
{

namespace
{
std::vector<LightSource> sLights;

// Id -> index into sLights, for update-in-place of moving lights.
// Only populated by registerOrUpdateLight; permanent registerLight
// entries stay off the map.
std::unordered_map<std::string, size_t>& idToIndex()
{
    static std::unordered_map<std::string, size_t> m;
    return m;
}
} // namespace

void registerLight(const LightSource& light)
{
    sLights.push_back(light);
}

void registerOrUpdateLight(const char* id, const LightSource& light)
{
    if (id == nullptr || id[0] == '\0')
    {
        sLights.push_back(light);
        return;
    }
    auto& map = idToIndex();
    auto it = map.find(id);
    if (it == map.end())
    {
        map.emplace(std::string(id), sLights.size());
        sLights.push_back(light);
        return;
    }
    sLights[it->second] = light;
}

void unregisterLight(const char* id)
{
    if (id == nullptr || id[0] == '\0')
        return;
    auto& map = idToIndex();
    auto it = map.find(id);
    if (it == map.end())
        return;
    const size_t idx = it->second;
    const size_t last = sLights.size() - 1;
    if (idx != last)
    {
        sLights[idx] = sLights[last];
        // Any id whose index was last now points at idx.
        for (auto& kv : map)
        {
            if (kv.second == last)
            {
                kv.second = idx;
                break;
            }
        }
    }
    sLights.pop_back();
    map.erase(it);
}

void clearLights()
{
    sLights.clear();
    idToIndex().clear();
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
