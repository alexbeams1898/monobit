#include "Growth.h"

#include <nlohmann/json.hpp>

#include <fstream>

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
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;

    loadNames(j, "faculties", state.faculties);
    loadNames(j, "secondary", state.secondary);

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
    const auto it = state.stat_levels.find(name);
    return it != state.stat_levels.end() ? it->second : 0;
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
