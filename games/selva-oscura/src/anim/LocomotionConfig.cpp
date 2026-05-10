#include "anim/LocomotionConfig.h"

#include <cstdio>
#include <fstream>

namespace selva::anim
{

bool LocomotionConfig::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[anim] LocomotionConfig: could not open %s\n", path.c_str());
        return false;
    }
    try
    {
        const auto j = nlohmann::json::parse(f);
        LocomotionConfigFile parsed;
        j.get_to(parsed);
        for (const auto& c : parsed.clips)
        {
            if (c.name.empty())
                continue;
            blend_in_by_clip[c.name] = c.blend_in_seconds;
            if (!c.family.empty())
                family_by_clip[c.name] = c.family;
        }
        std::fprintf(stderr, "[anim] LocomotionConfig: loaded %zu clip entry(ies) from %s\n",
                     blend_in_by_clip.size(), path.c_str());
        return true;
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "[anim] LocomotionConfig: failed to parse %s: %s\n", path.c_str(),
                     ex.what());
        return false;
    }
}

float LocomotionConfig::blendInSeconds(const std::string& clip_name, float default_seconds) const
{
    const auto it = blend_in_by_clip.find(clip_name);
    return it == blend_in_by_clip.end() ? default_seconds : it->second;
}

const std::string& LocomotionConfig::family(const std::string& clip_name) const
{
    static const std::string empty;
    const auto it = family_by_clip.find(clip_name);
    return it == family_by_clip.end() ? empty : it->second;
}

} // namespace selva::anim
