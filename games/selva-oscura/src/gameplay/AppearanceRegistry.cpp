#include "gameplay/AppearanceRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace selva::gameplay
{

AppearanceRegistry& AppearanceRegistry::instance()
{
    static AppearanceRegistry sInstance;
    return sInstance;
}

int AppearanceRegistry::loadFromFile(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        std::fprintf(stderr, "[appearance] AppearanceRegistry: could not open %s\n", path.c_str());
        return 0;
    }
    nlohmann::json j;
    try
    {
        f >> j;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[appearance] AppearanceRegistry: parse error in %s: %s\n",
                     path.c_str(), e.what());
        return 0;
    }
    defs_.clear();
    const auto sliders_it = j.find("sliders");
    if (sliders_it == j.end() || !sliders_it->is_array())
    {
        std::fprintf(stderr, "[appearance] AppearanceRegistry: %s missing 'sliders' array\n",
                     path.c_str());
        return 0;
    }
    for (const auto& s : *sliders_it)
    {
        AppearanceSliderDef def;
        def.id = s.value("id", "");
        def.label = s.value("label", def.id);
        def.min = s.value("min", 0.0f);
        def.max = s.value("max", 1.0f);
        def.default_value = s.value("default", 1.0f);
        def.applies_to = s.value("applies_to", "");
        def.param = s.value("param", "");
        def.param_decr = s.value("param_decr", "");
        def.param_incr = s.value("param_incr", "");
        def.category = s.value("category", "misc");
        def.player_visible = s.value("player_visible", true);
        if (def.id.empty() || def.applies_to.empty())
        {
            std::fprintf(stderr,
                         "[appearance] AppearanceRegistry: %s slider missing id or applies_to "
                         "(skipped)\n",
                         path.c_str());
            continue;
        }
        // applies_to-specific shape validation. Catch mis-authored
        // entries at load time rather than producing silent no-ops
        // at draw time.
        const bool is_pair =
            (def.applies_to == "morph_pair" || def.applies_to == "morph_pair_mirrored");
        if (is_pair)
        {
            if (def.param_decr.empty() || def.param_incr.empty())
            {
                std::fprintf(stderr,
                             "[appearance] AppearanceRegistry: %s slider '%s' kind '%s' "
                             "requires both param_decr + param_incr (skipped)\n",
                             path.c_str(), def.id.c_str(), def.applies_to.c_str());
                continue;
            }
            if (!def.param.empty())
            {
                std::fprintf(stderr,
                             "[appearance] AppearanceRegistry: %s slider '%s' kind '%s' "
                             "should NOT set 'param' (uses param_decr + param_incr); "
                             "ignoring stray param\n",
                             path.c_str(), def.id.c_str(), def.applies_to.c_str());
                def.param.clear();
            }
        }
        else
        {
            if (!def.param_decr.empty() || !def.param_incr.empty())
            {
                std::fprintf(stderr,
                             "[appearance] AppearanceRegistry: %s slider '%s' kind '%s' "
                             "ignores param_decr/param_incr (single-target kinds use 'param')\n",
                             path.c_str(), def.id.c_str(), def.applies_to.c_str());
            }
        }
        defs_.push_back(std::move(def));
    }
    std::fprintf(stderr, "[appearance] AppearanceRegistry: loaded %zu slider(s) from %s\n",
                 defs_.size(), path.c_str());
    return static_cast<int>(defs_.size());
}

const AppearanceSliderDef* AppearanceRegistry::find(const std::string& id) const
{
    for (const auto& d : defs_)
        if (d.id == id)
            return &d;
    return nullptr;
}

} // namespace selva::gameplay
