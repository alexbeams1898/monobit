#include "items/CategoryRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace selva::items
{

void CategoryRegistry::loadFromFile(const std::string& path)
{
    ordered.clear();
    std::ifstream in(path);
    if (!in)
    {
        std::fprintf(stderr, "[item-categories] cannot open %s\n", path.c_str());
        std::fflush(stderr);
        return;
    }
    try
    {
        nlohmann::json j;
        in >> j;
        if (!j.contains("categories") || !j.at("categories").is_array())
        {
            std::fprintf(stderr, "[item-categories] %s missing 'categories' array\n", path.c_str());
            std::fflush(stderr);
            return;
        }
        for (const auto& c : j.at("categories"))
        {
            CategoryDef def;
            def.id = c.value("id", std::string{});
            def.display_name = c.value("display_name", std::string{});
            def.order = c.value("order", 0);
            if (def.id.empty())
            {
                std::fprintf(stderr, "[item-categories] entry missing id; skipping\n");
                std::fflush(stderr);
                continue;
            }
            ordered.push_back(std::move(def));
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const CategoryDef& a, const CategoryDef& b) { return a.order < b.order; });
        std::fprintf(stderr, "[item-categories] loaded %zu categories from %s\n", ordered.size(),
                     path.c_str());
        std::fflush(stderr);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "[item-categories] parse error in %s: %s\n", path.c_str(), e.what());
        std::fflush(stderr);
    }
}

const CategoryDef* CategoryRegistry::get(const std::string& id) const
{
    for (const auto& c : ordered)
    {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

CategoryRegistry& categoryRegistry()
{
    static CategoryRegistry instance;
    return instance;
}

} // namespace selva::items
