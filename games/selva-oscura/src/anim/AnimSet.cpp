#include "anim/AnimSet.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace selva::anim
{

bool AnimSet::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[anim] AnimSet: could not open %s\n", path.c_str());
        return false;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& ex)
    {
        std::fprintf(stderr, "[anim] AnimSet: parse error in %s: %s\n", path.c_str(), ex.what());
        return false;
    }

    std::string set_id = j.value("set_id", "");
    std::string skeleton_id = j.value("skeleton_id", "");
    if (set_id.empty() || skeleton_id.empty())
    {
        std::fprintf(stderr, "[anim] AnimSet: %s missing set_id or skeleton_id\n", path.c_str());
        return false;
    }

    const auto clips_it = j.find("clips");
    if (clips_it == j.end() || !clips_it->is_object())
    {
        std::fprintf(stderr, "[anim] AnimSet: %s missing 'clips' object\n", path.c_str());
        return false;
    }

    std::array<std::string, static_cast<size_t>(AnimIntent::COUNT)> next_clips;
    for (auto it = clips_it->begin(); it != clips_it->end(); ++it)
    {
        const AnimIntent intent = animIntentFromName(it.key());
        if (intent == AnimIntent::COUNT)
        {
            std::fprintf(stderr,
                         "[anim] AnimSet: %s references unknown intent '%s' -- update "
                         "AnimIntent enum or fix the JSON\n",
                         path.c_str(), it.key().c_str());
            return false;
        }
        const std::string& key = it.value().get_ref<const std::string&>();
        if (key.empty())
        {
            std::fprintf(stderr, "[anim] AnimSet: %s intent '%s' has empty clip key\n",
                         path.c_str(), it.key().c_str());
            return false;
        }
        next_clips[static_cast<size_t>(intent)] = key;
    }

    // Validate every intent has a mapping. Missing entries fail loud
    // here rather than silently at request time.
    for (size_t i = 0; i < next_clips.size(); ++i)
    {
        if (next_clips[i].empty())
        {
            std::fprintf(stderr, "[anim] AnimSet: %s missing mapping for intent '%s'\n",
                         path.c_str(),
                         std::string(animIntentName(static_cast<AnimIntent>(i))).c_str());
            return false;
        }
    }

    set_id_ = std::move(set_id);
    skeleton_id_ = std::move(skeleton_id);
    clip_by_intent_ = std::move(next_clips);
    std::fprintf(stderr, "[anim] AnimSet: loaded %s (skeleton=%s) from %s\n", set_id_.c_str(),
                 skeleton_id_.c_str(), path.c_str());
    return true;
}

std::string_view AnimSet::clipKey(AnimIntent intent) const
{
    const auto i = static_cast<size_t>(intent);
    return i < clip_by_intent_.size() ? std::string_view{clip_by_intent_[i]} : std::string_view{};
}

} // namespace selva::anim
