#include "anim/LocomotionConfig.h"

#include <cstdio>
#include <fstream>

namespace selva::anim
{

TranslationSource parseTranslationSource(const std::string& s)
{
    if (s == "root_motion")
        return TranslationSource::RootMotion;
    if (s == "in_place")
        return TranslationSource::InPlace;
    if (s == "velocity" || s.empty())
        return TranslationSource::Velocity;
    std::fprintf(stderr,
                 "[anim] LocomotionConfig: unknown translation_source '%s' — defaulting to "
                 "'velocity'\n",
                 s.c_str());
    return TranslationSource::Velocity;
}

const char* translationSourceName(TranslationSource src)
{
    switch (src)
    {
    case TranslationSource::Velocity:
        return "velocity";
    case TranslationSource::RootMotion:
        return "root_motion";
    case TranslationSource::InPlace:
        return "in_place";
    }
    return "?";
}

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
            source_by_clip[c.name] = parseTranslationSource(c.translation_source);
            if (c.playback_rate != 1.0f)
                playback_rate_by_clip[c.name] = c.playback_rate;
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

TranslationSource LocomotionConfig::translationSource(const std::string& clip_name) const
{
    const auto it = source_by_clip.find(clip_name);
    return it == source_by_clip.end() ? TranslationSource::Velocity : it->second;
}

float LocomotionConfig::playbackRate(const std::string& clip_name) const
{
    const auto it = playback_rate_by_clip.find(clip_name);
    return it == playback_rate_by_clip.end() ? 1.0f : it->second;
}

} // namespace selva::anim
