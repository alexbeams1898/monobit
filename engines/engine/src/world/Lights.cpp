#include "world/Lights.h"

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

} // namespace engine::world
