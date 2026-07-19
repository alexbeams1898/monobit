#include "Growth.h"

#include "JsonConfig.h"

#include <nlohmann/json.hpp>

namespace growth
{

namespace
{
void loadNames(const nlohmann::json& j, const char* key, std::vector<std::string>& out)
{
    for (const auto& e : j.value(key, nlohmann::json::array()))
        if (e.is_string())
            out.push_back(e.get<std::string>());
}
} // namespace

void load(GrowthState& state, const std::string& path)
{
    state.faculties.clear();
    state.secondary.clear();
    state.buff_defs.clear();
    state.faculty_colors.clear();
    const auto loaded = config::load(path);
    if (!loaded)
        return;
    const nlohmann::json& j = *loaded;

    loadNames(j, "faculties", state.faculties);
    loadNames(j, "secondary", state.secondary);
    state.exp_per_level = j.value("exp_per_level", state.exp_per_level);

    // Starting stat levels (everything else begins at 0). Placeholder until the EXP->faculty
    // progression is built -- see faculties.json.
    if (const auto it = j.find("starting_levels"); it != j.end() && it->is_object())
        for (const auto& [name, lvl] : it->items())
            if (lvl.is_number_integer())
                state.stat_levels[name] = lvl.get<int>();

    if (const auto it = j.find("faculty_colors"); it != j.end() && it->is_object())
        for (const auto& [name, arr] : it->items())
            if (arr.is_array() && arr.size() == 3)
                state.faculty_colors[name] =
                    Rgb{arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>()};

    for (const auto& e : j.value("buffs", nlohmann::json::array()))
    {
        BuffDef b;
        b.id = e.value("id", std::string{});
        b.faculty = e.value("faculty", std::string{});
        b.max_level = e.value("max_level", 1);
        if (!b.id.empty())
            state.buff_defs.push_back(std::move(b));
    }
}

int statLevel(const GrowthState& state, const std::string& name)
{
    const auto base = state.stat_levels.find(name);
    int level = base != state.stat_levels.end() ? base->second : 0;
    // Add the level earned by USE: floor(log2(use / exp_per_level + 1)) -- diminishing, so the
    // first level is cheap and later ones cost progressively more. The level is DERIVED here,
    // never stored (stat_use is the saved fact).
    const auto use = state.stat_use.find(name);
    if (use != state.stat_use.end() && use->second > 0 && state.exp_per_level > 0.0f)
    {
        const float ratio = static_cast<float>(use->second) / state.exp_per_level + 1.0f;
        level += static_cast<int>(std::floor(std::log2(ratio)));
    }
    return level;
}

void recordUse(GrowthState& state, const std::string& name, int exp)
{
    if (name.empty() || exp <= 0)
        return;
    state.stat_use[name] += exp;
}

StatProgress statProgress(const GrowthState& state, const std::string& name)
{
    StatProgress p;
    if (state.exp_per_level <= 0.0f)
        return p;
    const auto it = state.stat_use.find(name);
    const int use = it != state.stat_use.end() ? it->second : 0;
    // Level L begins at k*(2^L - 1) use and the next at k*(2^(L+1) - 1) -- inverting the
    // curve level = floor(log2(use/k + 1)). The bar fills across THIS level's span, so it
    // renormalizes each level (a rising span, a fixed-width bar; see docs §5 / research).
    const float k = state.exp_per_level;
    if (use <= 0)
        return p; // no use -> level 0, empty bar
    // The USE-derived level (the bar's level). The stat's DISPLAYED level is base + this
    // (statLevel); the bar tracks progress of the use portion, which is what grows.
    p.level = static_cast<int>(std::floor(std::log2(static_cast<float>(use) / k + 1.0f)));
    const int levelStart = static_cast<int>(k * (std::pow(2.0f, p.level) - 1.0f));
    const int nextStart = static_cast<int>(k * (std::pow(2.0f, p.level + 1) - 1.0f));
    p.into = use - levelStart;
    p.span = nextStart - levelStart;
    p.fill = p.span > 0
                 ? std::clamp(static_cast<float>(p.into) / static_cast<float>(p.span), 0.0f, 1.0f)
                 : 0.0f;
    return p;
}

int spirit(const GrowthState& state)
{
    int total = 0;
    for (const auto& [id, level] : state.buff_levels)
        total += level;
    return total;
}

Rgb facultyColor(const GrowthState& state, const std::string& faculty)
{
    const auto it = state.faculty_colors.find(faculty);
    return it != state.faculty_colors.end() ? it->second : Rgb{0.95f, 0.95f, 0.92f};
}

int facultyLevel(const GrowthState& state, const std::string& faculty)
{
    // Base stat value plus the levels of the buffs belonging to this faculty.
    int total = statLevel(state, faculty);
    for (const auto& def : state.buff_defs)
    {
        if (def.faculty != faculty)
            continue;
        const auto it = state.buff_levels.find(def.id);
        if (it != state.buff_levels.end())
            total += it->second;
    }
    return total;
}

} // namespace growth
