#include "ops/GuideOps.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace guide
{

std::string number(int index)
{
    const int n = index + 1;
    return (n < 10 ? "0" : "") + std::to_string(n);
}

std::string nameOf(const std::string& species)
{
    return std::filesystem::path(species).stem().string();
}

Page load(const std::string& path)
{
    Page p;
    p.species = path;
    p.name = nameOf(path);
    std::ifstream in(path);
    if (!in)
        return p;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return p;
    p.sprite = j.value("sprite", std::string{});
    const auto& sheet = j.value("sheet", nlohmann::json::object());
    p.resistance = sheet.value("resistance", 0);
    p.defensiveness = sheet.value("defensiveness", 0);
    p.dispersal = sheet.value("dispersal", 0);
    if (const auto it = j.find("guide"); it != j.end() && it->is_object())
    {
        p.printed = true;
        p.entry.description = it->value("description", std::string{});
        p.entry.similar = it->value("similar", std::string{});
        p.entry.biology = it->value("biology", std::string{});
        p.entry.habits = it->value("habits", std::string{});
        p.entry.signs = it->value("signs", std::string{});
        p.entry.control = it->value("control", std::string{});
    }
    return p;
}

std::vector<Page> scan(const std::string& dir)
{
    std::vector<Page> pages;
    std::error_code ec; // a missing directory is an empty book, not a crash
    for (const auto& f : std::filesystem::directory_iterator(dir, ec))
    {
        if (f.path().extension() != ".json")
            continue;
        pages.push_back(load(f.path().generic_string()));
    }
    std::sort(pages.begin(), pages.end(),
              [](const Page& a, const Page& b) { return a.species < b.species; });
    return pages;
}

Gates gates(const std::string& statsPath)
{
    Gates g;
    std::ifstream in(statsPath);
    if (!in)
        return g;
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded())
        return g;
    const auto& gj = j.value("guide", nlohmann::json::object());
    g.kills_for_entry = gj.value("kills_for_entry", g.kills_for_entry);
    g.kills_for_stats = gj.value("kills_for_stats", g.kills_for_stats);
    return g;
}

Tier tier(int kills, const Gates& g)
{
    if (kills >= g.kills_for_stats)
        return Tier::Stats;
    if (kills >= g.kills_for_entry)
        return Tier::Entry;
    return Tier::Undocumented;
}

} // namespace guide
