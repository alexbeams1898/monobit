#include "render/ActiveLightingEnv.h"

namespace selva::render
{

namespace
{
ActiveLightingEnv sEnv{};
}

void setActiveLightingEnv(const ActiveLightingEnv& env)
{
    sEnv = env;
}

const ActiveLightingEnv& activeLightingEnv()
{
    return sEnv;
}

} // namespace selva::render
