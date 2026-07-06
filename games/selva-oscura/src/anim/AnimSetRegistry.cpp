#include "anim/AnimSetRegistry.h"

#include <cstdio>
#include <filesystem>

namespace selva::anim
{

AnimSetRegistry& AnimSetRegistry::instance()
{
    static AnimSetRegistry sInstance;
    return sInstance;
}

int AnimSetRegistry::loadFromDirectory(const std::string& dir)
{
    namespace fs = std::filesystem;
    if (!fs::is_directory(dir))
    {
        std::fprintf(stderr, "[anim] AnimSetRegistry: not a directory: %s\n", dir.c_str());
        return 0;
    }
    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        auto set = std::make_unique<AnimSet>();
        if (!set->loadFromFile(entry.path().string()))
            continue;
        const std::string id = set->id();
        sets_[id] = std::move(set);
        ++loaded;
    }
    std::fprintf(stderr, "[anim] AnimSetRegistry: loaded %d set(s) from %s\n", loaded, dir.c_str());
    return loaded;
}

const AnimSet* AnimSetRegistry::get(std::string_view set_id) const
{
    const auto it = sets_.find(std::string(set_id));
    return it == sets_.end() ? nullptr : it->second.get();
}

} // namespace selva::anim
