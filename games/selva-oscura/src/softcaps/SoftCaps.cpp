#include "softcaps/SoftCaps.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace selva::softcaps
{

namespace
{

struct Curve
{
    int bend_at = 1;
    float post_bend_multiplier = 1.0f;
};

// Per derived-value, per class. Outer key = derived_key
// ("hp_from_end" etc); inner key = playerClassName() value.
std::unordered_map<std::string, std::unordered_map<std::string, Curve>>& curves()
{
    static std::unordered_map<std::string, std::unordered_map<std::string, Curve>> m;
    return m;
}

// Map PlayerClass enum to the JSON key string that authoring uses.
// Must stay in sync with the lowercase keys in soft_caps.json
// ("penitent" / "heretic" / "ferine" / "unburdened").
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
        std::fprintf(stderr, "[softcaps] cannot open %s; keeping previous curves\n",
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
        std::fprintf(stderr, "[softcaps] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return false;
    }

    auto& table = curves();
    table.clear();

    int loaded = 0;
    for (auto& [derived_key, classes_block] : j.items())
    {
        // Skip _comment / _schema metadata keys.
        if (!derived_key.empty() && derived_key[0] == '_')
            continue;
        if (!classes_block.is_object())
            continue;

        for (auto& [class_key, curve_obj] : classes_block.items())
        {
            if (!curve_obj.is_object())
                continue;
            Curve c;
            c.bend_at = curve_obj.value("bend_at", 1);
            c.post_bend_multiplier = curve_obj.value("post_bend_multiplier", 1.0f);
            table[derived_key][class_key] = c;
            ++loaded;
        }
    }

    std::fprintf(stderr, "[softcaps] loaded %d curves from %s\n", loaded, path.c_str());
    std::fflush(stderr);
    return loaded > 0;
}

float apply(float stat_value, PlayerClass cls, const std::string& derived_key)
{
    if (cls == PlayerClass::None)
        return stat_value;

    const char* class_key = configKeyFor(cls);
    if (class_key == nullptr || class_key[0] == '\0')
        return stat_value;

    const auto& table = curves();
    const auto outer = table.find(derived_key);
    if (outer == table.end())
        return stat_value;

    const auto inner = outer->second.find(class_key);
    if (inner == outer->second.end())
        return stat_value;

    const Curve& c = inner->second;
    // No-op cap: multiplier >= 1.0 means linear forever.
    if (c.post_bend_multiplier >= 1.0f)
        return stat_value;

    const float bend = static_cast<float>(c.bend_at);
    if (stat_value <= bend)
        return stat_value;

    // Above bend: full returns up to bend + fractional above.
    return bend + (stat_value - bend) * c.post_bend_multiplier;
}

} // namespace selva::softcaps
