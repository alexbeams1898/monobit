#include "identity/Identity.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <unordered_map>

namespace selva::identity
{

namespace
{

// One identity-stat function = ordered list of (input_key, weight)
// pairs. Iteration evaluates each input against the profile and
// accumulates input_value * weight.
struct Function
{
    std::vector<std::pair<std::string, float>> inputs;
};

// Per-class function table. Empty entries (no key for a class) yield
// 0 from compute(). Keyed by playerClassName() return value so
// loader + lookup match the canonical string.
std::unordered_map<std::string, Function>& functions()
{
    static std::unordered_map<std::string, Function> m;
    return m;
}

// Resolve one input key against the profile + active class. v1
// supports:
//   "signing_anchor" -> 1 if signing_committed flag set AND the
//                       profile's current class equals the class
//                       being evaluated; else 0. Anchors the first
//                       identity-stat point in the cosmological act
//                       of signing itself.
// Unknown keys log + contribute 0. New input kinds register here as
// gameplay systems land (counter:hits_tanked, flag:tanked_lupa,
// lifetime:sangue, lifetime:riversamento, etc.).
float evalInput(const std::string& key, const PlayerProfile& profile, PlayerClass cls)
{
    if (key == "signing_anchor")
    {
        const auto& flags = profile.flags;
        const bool signed_committed =
            std::find(flags.begin(), flags.end(), std::string{"signing_committed"}) != flags.end();
        return (signed_committed && profile.player_class == cls) ? 1.0f : 0.0f;
    }
    std::fprintf(stderr, "[identity] unknown input key '%s' in formula; contributing 0\n",
                 key.c_str());
    std::fflush(stderr);
    return 0.0f;
}

// Resolve which JSON key matches a PlayerClass. Matches the loader
// keys ("penitent" / "heretic" / "ferine" / "unburdened"). None
// returns empty.
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
        std::fprintf(stderr, "[identity] cannot open %s; keeping previous functions\n",
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
        std::fprintf(stderr, "[identity] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
        return false;
    }

    auto& fns = functions();
    fns.clear();

    int loaded = 0;
    for (auto& [class_key, entry] : j.items())
    {
        // Skip _comment / _inputs style metadata keys; they document
        // the schema for human authors but aren't class names.
        if (!class_key.empty() && class_key[0] == '_')
            continue;
        if (!entry.is_object())
            continue;

        Function fn;
        for (auto& [input_key, weight] : entry.items())
        {
            if (!weight.is_number())
                continue;
            fn.inputs.emplace_back(input_key, weight.get<float>());
        }
        if (!fn.inputs.empty())
        {
            fns[class_key] = std::move(fn);
            ++loaded;
        }
    }

    std::fprintf(stderr, "[identity] loaded %d class functions from %s\n", loaded, path.c_str());
    std::fflush(stderr);
    return loaded > 0;
}

int computeIdentityStat(const PlayerProfile& profile, PlayerClass cls)
{
    if (cls == PlayerClass::None)
        return 0;

    const char* key = configKeyFor(cls);
    if (key == nullptr || key[0] == '\0')
        return 0;

    const auto& fns = functions();
    const auto it = fns.find(key);
    if (it == fns.end())
        return 0;

    float accum = 0.0f;
    for (const auto& [input_key, weight] : it->second.inputs)
        accum += evalInput(input_key, profile, cls) * weight;

    // Floor to int at the boundary; fractional weights accumulate
    // across many counter ticks and only "tip over" to the next stat
    // point at thresholds, which reads cleanly to the player as
    // "doing X N times earned me a point."
    if (accum < 0.0f)
        accum = 0.0f;
    return static_cast<int>(std::floor(accum));
}

} // namespace selva::identity
