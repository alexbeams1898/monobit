#include "items/ItemRegistry.h"

#include "ecs/ConfigLoaders.h"
#include "items/CategoryRegistry.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace selva::items
{

namespace
{

std::unordered_map<std::string, ItemExtensions>& extensions()
{
    static std::unordered_map<std::string, ItemExtensions> m;
    return m;
}

// engine ItemCategory -> bucket-key string the inventory UI uses.
// Boot-time validation cross-checks each item's bucket against the
// CategoryRegistry so a typo in a JSON `category` field fails loudly
// at startup, not silently at use.
const char* bucketKeyFor(engine::ecs::ItemCategory c)
{
    switch (c)
    {
    case engine::ecs::ItemCategory::Weapon:
        return "weapons";
    case engine::ecs::ItemCategory::Armor:
        return "armor";
    case engine::ecs::ItemCategory::Consumable:
        return "consumables";
    case engine::ecs::ItemCategory::KeyItem:
        return "key_items";
    case engine::ecs::ItemCategory::Material:
        return "materials";
    case engine::ecs::ItemCategory::Money:
        return "money";
    case engine::ecs::ItemCategory::Accessory:
        return "accessories";
    case engine::ecs::ItemCategory::Incantation:
        return "incantations";
    case engine::ecs::ItemCategory::Invocation:
        return "invocations";
    }
    return "materials";
}

} // namespace

engine::ecs::ItemRegistry& itemRegistry()
{
    static engine::ecs::ItemRegistry instance;
    return instance;
}

const ItemExtensions* itemExtensions(const std::string& config_path)
{
    const auto it = extensions().find(config_path);
    return (it == extensions().end()) ? nullptr : &it->second;
}

void loadItemDirectory(const std::filesystem::path& dir)
{
    itemRegistry().defs.clear();
    extensions().clear();

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        std::fprintf(stderr, "[items] directory not found: %s\n", dir.string().c_str());
        std::fflush(stderr);
        return;
    }

    // Engine owns the canonical ItemDef parse -- name, category,
    // rarity, base_damage, all body+mind scaling/requirements, weapon
    // visuals, armor stats, the lot. We delegate to it so Selva
    // doesn't duplicate (and silently drift from) the engine schema.
    engine::ecs::loadItemRegistry(itemRegistry(), dir.generic_string());

    // Second pass: validate categories against the CategoryRegistry
    // and parse Selva-only extension fields into the side-table.
    // Walking the JSON twice is cheap (boot-time, dozens of files);
    // the alternative is widening the engine loader with a callback
    // hook, which leaks Selva concerns up into the engine.
    const auto& cats = categoryRegistry();

    for (auto it = itemRegistry().defs.begin(); it != itemRegistry().defs.end();)
    {
        const std::string& config_path = it->first;
        const engine::ecs::ItemDef& def = it->second;

        const char* bucket = bucketKeyFor(def.category);
        if (cats.get(bucket) == nullptr)
        {
            std::fprintf(stderr,
                         "[items] '%s' has bucket '%s' not declared in "
                         "inventory_categories.json; dropping\n",
                         config_path.c_str(), bucket);
            std::fflush(stderr);
            it = itemRegistry().defs.erase(it);
            continue;
        }

        std::ifstream in(config_path);
        if (in)
        {
            try
            {
                nlohmann::json j;
                in >> j;

                ItemExtensions ext;
                ext.use_handler = j.value("use_handler", std::string{});
                ext.use_condition = j.value("use_condition", std::string{});
                ext.identity_class = j.value("identity_class", std::string{});
                ext.identity_stat_scaling = j.value("identity_stat_scaling", 0.0f);
                ext.identity_stat_requirement = j.value("identity_stat_requirement", 0);

                const bool has_extension = !ext.use_handler.empty() || !ext.use_condition.empty() ||
                                           !ext.identity_class.empty();
                if (has_extension)
                {
                    if (!ext.identity_class.empty())
                        std::fprintf(stderr,
                                     "[items-ext] '%s' identity_class=%s scaling=%.2f req=%d\n",
                                     config_path.c_str(), ext.identity_class.c_str(),
                                     static_cast<double>(ext.identity_stat_scaling),
                                     ext.identity_stat_requirement);
                    extensions()[config_path] = std::move(ext);
                }
            }
            catch (const std::exception& e)
            {
                std::fprintf(stderr, "[items] extension parse error in %s: %s\n",
                             config_path.c_str(), e.what());
                std::fflush(stderr);
            }
        }

        std::fprintf(stderr, "[items] '%s' (bucket=%s)\n", config_path.c_str(), bucket);
        std::fflush(stderr);
        ++it;
    }
}

engine::ecs::RecipeRegistry& recipeRegistry()
{
    static engine::ecs::RecipeRegistry instance;
    return instance;
}

void loadRecipeDirectory(const std::filesystem::path& dir)
{
    recipeRegistry().recipes.clear();
    recipeRegistry().loaded = false;
    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    {
        std::fprintf(stderr, "[recipes] directory not found: %s\n", dir.string().c_str());
        std::fflush(stderr);
        return;
    }
    engine::ecs::loadRecipeRegistry(recipeRegistry(), dir.generic_string());
}

} // namespace selva::items
