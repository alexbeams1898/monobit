#include "Growth.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace growth
{

void load(GrowthState& state, const std::string& path)
{
    state.faculties.clear();
    state.buff_defs.clear();
    std::ifstream f(path);
    if (!f)
        return;
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return;

    for (const auto& e : j.value("faculties", nlohmann::json::array()))
        if (e.is_string())
            state.faculties.push_back(e.get<std::string>());

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

int spirit(const GrowthState& state)
{
    int total = 0;
    for (const auto& [id, level] : state.buff_levels)
        total += level;
    return total;
}

int facultyLevel(const GrowthState& state, const std::string& faculty)
{
    int total = 0;
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
