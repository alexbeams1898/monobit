#include "items/ItemRegistry.h"

#include "AppState.h"
#include "AppStateGlobal.h"
#include "combat/QuickSlot.h"
#include "ecs/ConfigLoaders.h"
#include "items/CategoryRegistry.h"
#include "ops/CraftingOps.h"

#include <algorithm>
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

bool craftAndRecord(const engine::ecs::RecipeDef& recipe)
{
    selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    if (!engine::ops::crafting::craft(profile->inventory, recipe, itemRegistry()))
        return false;

    // QoL: auto-assign the just-crafted output (if it's a Consumable
    // and the setting is on). Same hook used by loot pickups so both
    // grant paths honor the toggle uniformly.
    selva::combat::tryAutoAssignOnGrant(recipe.output_item);

    const std::uint32_t new_count = ++profile->craft_counts[recipe.config_path];

    if (!recipe.unlocks_recipe.empty() && recipe.unlock_after > 0 &&
        new_count >= static_cast<std::uint32_t>(recipe.unlock_after))
    {
        const auto& known = profile->known_recipes;
        if (std::find(known.begin(), known.end(), recipe.unlocks_recipe) == known.end())
        {
            profile->known_recipes.push_back(recipe.unlocks_recipe);
            std::fprintf(stderr, "[craft] unlocked '%s' after %u crafts of '%s'\n",
                         recipe.unlocks_recipe.c_str(), new_count, recipe.config_path.c_str());
            std::fflush(stderr);
        }
    }
    return true;
}

bool isRecipeKnown(const std::string& recipe_path)
{
    const selva::PlayerProfile* profile = selva::activePlayerProfile();
    if (profile == nullptr)
        return false;
    const auto& k = profile->known_recipes;
    return std::find(k.begin(), k.end(), recipe_path) != k.end();
}

bool itemFitsSlot(const engine::ecs::ItemDef& def, engine::ecs::EquipSlot slot)
{
    using engine::ecs::ArmorSlot;
    using engine::ecs::EquipSlot;
    using engine::ecs::ItemCategory;
    switch (slot)
    {
    case EquipSlot::RightHand:
    case EquipSlot::LeftHand:
        return def.category == ItemCategory::Weapon ||
               def.category == ItemCategory::Incantation ||
               def.category == ItemCategory::Invocation;
    case EquipSlot::Head:
        return def.category == ItemCategory::Armor && def.armor_slot == ArmorSlot::Head;
    case EquipSlot::Chest:
        return def.category == ItemCategory::Armor && def.armor_slot == ArmorSlot::Chest;
    case EquipSlot::Legs:
        return def.category == ItemCategory::Armor && def.armor_slot == ArmorSlot::Legs;
    case EquipSlot::Feet:
        return def.category == ItemCategory::Armor && def.armor_slot == ArmorSlot::Feet;
    case EquipSlot::Accessory1:
    case EquipSlot::Accessory2:
        return def.category == ItemCategory::Accessory;
    }
    return false;
}

std::vector<const engine::ecs::ItemInstance*>
collectItemsFittingSlot(const engine::ecs::Inventory& inv,
                        const engine::ecs::ItemRegistry& items, engine::ecs::EquipSlot slot)
{
    std::vector<const engine::ecs::ItemInstance*> out;
    for (const auto& [cat_key, bucket] : inv.by_category)
    {
        for (const auto& it : bucket)
        {
            const engine::ecs::ItemDef* def = items.find(it.config_path);
            if (def != nullptr && itemFitsSlot(*def, slot))
                out.push_back(&it);
        }
    }
    return out;
}

std::string itemDisplayName(const engine::ecs::ItemInstance& it,
                            const engine::ecs::ItemRegistry& items)
{
    const engine::ecs::ItemDef* def = items.find(it.config_path);
    const std::string base = (def != nullptr && !def->name.empty()) ? def->name : it.config_path;
    if (it.quality == engine::ecs::QualityTier::Common)
        return base;
    return std::string(engine::ecs::qualityName(it.quality)) + " " + base;
}

} // namespace selva::items
