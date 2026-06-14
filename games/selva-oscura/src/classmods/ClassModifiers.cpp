#include "classmods/ClassModifiers.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace selva::classmods
{

namespace
{

// Per modifier key, per class. Outer key = modifier name
// ("hp_offset" etc); inner key = playerClassName() value.
std::unordered_map<std::string, std::unordered_map<std::string, int>>& modifiers()
{
    static std::unordered_map<std::string, std::unordered_map<std::string, int>> m;
    return m;
}

// Map PlayerClass enum to the JSON key string. Must stay in sync
// with class_modifiers.json keys ("penitent" / "heretic" /
// "ferine" / "unburdened").
const char* configKeyFor(PlayerClass cls)
{
    switch (cls)
    {
    case PlayerClass::Penitent:
        return "penitent";
    case PlayerClass::Heretic:
        return "heretic";
    case PlayerClass::Ferine:
        return "ferine";
    case PlayerClass::Unburdened:
        return "unburdened";
    case PlayerClass::None:
    default:
        return "";
    }
}

} // namespace

bool loadFromFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "[classmods] cannot open %s; keeping previous modifiers\n",
                     path.c_str());
        std::fflush(stderr);
        return false;
    }

    nlohmann::json j;
    try
    {
        in >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[classmods] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return false;
    }

    auto& table = modifiers();
    table.clear();

    int loaded = 0;
    for (auto& [mod_key, classes_block] : j.items())
    {
        if (!mod_key.empty() && mod_key[0] == '_')
            continue;
        if (!classes_block.is_object())
            continue;

        for (auto& [class_key, val] : classes_block.items())
        {
            if (!val.is_number())
                continue;
            table[mod_key][class_key] = val.get<int>();
            ++loaded;
        }
    }

    std::fprintf(stderr, "[classmods] loaded %d modifiers from %s\n", loaded, path.c_str());
    std::fflush(stderr);
    return loaded > 0;
}

int offsetFor(PlayerClass cls, const std::string& modifier_key)
{
    if (cls == PlayerClass::None)
        return 0;

    const char* class_key = configKeyFor(cls);
    if (class_key == nullptr || class_key[0] == '\0')
        return 0;

    const auto& table = modifiers();
    const auto outer = table.find(modifier_key);
    if (outer == table.end())
        return 0;

    const auto inner = outer->second.find(class_key);
    if (inner == outer->second.end())
        return 0;

    return inner->second;
}

} // namespace selva::classmods
