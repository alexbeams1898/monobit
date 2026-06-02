#include "items/ItemRegistry.h"

#include "items/CategoryRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace selva::items
{

const char* entryKindName(EntryKind k)
{
    switch (k)
    {
    case EntryKind::Possession: return "possession";
    case EntryKind::Stack: return "stack";
    case EntryKind::Instanced: return "instanced";
    }
    return "?";
}

EntryKind parseEntryKind(const std::string& s)
{
    if (s == "stack")
        return EntryKind::Stack;
    if (s == "instanced")
        return EntryKind::Instanced;
    return EntryKind::Possession; // default + "possession"
}

void ItemRegistry::loadDirectory(const std::filesystem::path& dir)
{
    by_id.clear();
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        std::fprintf(stderr, "[items] directory not found: %s\n", dir.string().c_str());
        std::fflush(stderr);
        return;
    }
    const auto& cats = categoryRegistry();
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".json")
            continue;
        std::ifstream in(entry.path());
        if (!in)
        {
            std::fprintf(stderr, "[items] cannot open %s\n", entry.path().string().c_str());
            std::fflush(stderr);
            continue;
        }
        try
        {
            nlohmann::json j;
            in >> j;
            ItemDef def;
            def.id = j.value("id", std::string{});
            def.display_name = j.value("display_name", std::string{});
            def.description = j.value("description", std::string{});
            def.category = j.value("category", std::string{});
            def.kind = parseEntryKind(j.value("kind", std::string("possession")));
            def.icon_path = j.value("icon", std::string{});
            def.use_handler = j.value("use_handler", std::string{});
            def.use_condition = j.value("use_condition", std::string{});
            if (def.id.empty())
            {
                std::fprintf(stderr, "[items] %s has empty id; skipping\n",
                             entry.path().string().c_str());
                std::fflush(stderr);
                continue;
            }
            if (def.category.empty() || cats.get(def.category) == nullptr)
            {
                std::fprintf(stderr, "[items] '%s' references unknown category '%s'; skipping\n",
                             def.id.c_str(), def.category.c_str());
                std::fflush(stderr);
                continue;
            }
            const std::string id = def.id;
            by_id[id] = std::move(def);
            std::fprintf(stderr, "[items] loaded '%s' (kind=%s, category=%s) from %s\n",
                         id.c_str(), entryKindName(by_id[id].kind), by_id[id].category.c_str(),
                         entry.path().string().c_str());
            std::fflush(stderr);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "[items] parse error in %s: %s\n",
                         entry.path().string().c_str(), e.what());
            std::fflush(stderr);
        }
    }
}

const ItemDef* ItemRegistry::get(const std::string& id) const
{
    const auto it = by_id.find(id);
    return (it == by_id.end()) ? nullptr : &it->second;
}

ItemRegistry& itemRegistry()
{
    static ItemRegistry instance;
    return instance;
}

} // namespace selva::items
