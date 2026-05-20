#include "anim/ClipRegistry.h"

#include <cstdio>
#include <filesystem>

namespace selva::anim
{

int ClipRegistry::loadDirectory(const std::string& dir)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec))
    {
        std::fprintf(stderr, "[anim] ClipRegistry: directory not found: %s\n", dir.c_str());
        return 0;
    }
    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(dir, ec))
    {
        if (!entry.is_regular_file())
            continue;
        const auto& path = entry.path();
        if (path.extension() != ".ozz")
            continue;
        const std::string stem = path.stem().string();
        // skeleton.ozz is a skeleton archive, not an animation. Loading
        // it as a clip emits "not an animation" warnings — skip.
        if (stem == "skeleton")
            continue;
        AnimationClip clip = loadAnimationClip(path.string());
        if (!clip.isLoaded())
        {
            std::fprintf(stderr, "[anim] ClipRegistry: failed to load %s\n", path.string().c_str());
            continue;
        }
        by_name.emplace(stem, std::move(clip));
        ++loaded;
    }
    return loaded;
}

bool ClipRegistry::loadClip(const std::string& path, const std::string& name)
{
    AnimationClip clip = loadAnimationClip(path);
    if (!clip.isLoaded())
        return false;
    by_name.insert_or_assign(name, std::move(clip));
    return true;
}

const AnimationClip* ClipRegistry::get(const std::string& name) const
{
    const auto it = by_name.find(name);
    return it == by_name.end() ? nullptr : &it->second;
}

} // namespace selva::anim
