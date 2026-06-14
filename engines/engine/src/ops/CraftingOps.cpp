#include "ops/CraftingOps.h"

#include "ops/InventoryOps.h"

#include <algorithm>
#include <vector>

namespace engine::ops::crafting
{

using engine::ecs::ItemDef;
using engine::ecs::ItemInstance;
using engine::ecs::QualityTier;

namespace
{

// Consume a quantity of `config_path`, smallest-quality stacks first
// (preserve best materials for future crafts). Removes empty stacks.
// Returns the sum of quality-tier-indices consumed for averaging the
// output quality.
int consumeItemForCraft(Inventory& inv, const std::string& config_path, int quantity)
{
    // Collect (category_key, index) pairs of all matching stacks across
    // every bucket, sort by quality, then consume in order.
    struct Hit
    {
        std::string bucket;
        int index;
        int quality_idx;
    };
    std::vector<Hit> matches;
    for (auto& [bucket, items] : inv.by_category)
    {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            if (items[i].config_path == config_path)
                matches.push_back({bucket, i, static_cast<int>(items[i].quality)});
        }
    }

    std::sort(matches.begin(), matches.end(),
              [](const Hit& a, const Hit& b) { return a.quality_idx < b.quality_idx; });

    int remaining = quantity;
    int quality_sum = 0;
    for (const auto& m : matches)
    {
        if (remaining <= 0)
            break;
        auto& stack = inv.by_category[m.bucket][m.index];
        const int take = std::min(remaining, stack.quantity);
        quality_sum += static_cast<int>(stack.quality) * take;
        stack.quantity -= take;
        remaining -= take;
    }

    // Remove empty stacks (reverse index order within each bucket so
    // erasing one doesn't invalidate the indices of others we still
    // want to touch).
    for (auto& [bucket, items] : inv.by_category)
    {
        for (int i = static_cast<int>(items.size()) - 1; i >= 0; --i)
        {
            if (items[i].quantity <= 0 && items[i].config_path == config_path)
                items.erase(items.begin() + i);
        }
    }

    return quality_sum;
}

} // namespace

bool canCraft(const Inventory& inv, const RecipeDef& recipe, const ItemRegistry& /*registry*/)
{
    for (const auto& ing : recipe.inputs)
    {
        if (engine::ops::inventory::countItem(inv, ing.config_path) < ing.quantity)
            return false;
    }
    return true;
}

bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry,
           bool free_materials)
{
    if (!canCraft(inv, recipe, registry))
        return false;

    // Consume ingredients, tracking quality for output.
    int total_quality = 0;
    int total_items = 0;
    if (!free_materials)
    {
        for (const auto& ing : recipe.inputs)
        {
            total_quality += consumeItemForCraft(inv, ing.config_path, ing.quantity);
            total_items += ing.quantity;
        }
    }

    // Output quality = rounded average of input qualities.
    const int avg_quality =
        (total_items > 0) ? ((total_quality + total_items / 2) / total_items) : 1;
    const auto output_quality =
        static_cast<QualityTier>(std::min(avg_quality, static_cast<int>(QualityTier::Masterwork)));

    ItemInstance output;
    output.config_path = recipe.output_item;
    output.quantity = recipe.output_quantity;
    output.quality = output_quality;
    engine::ops::inventory::addItem(inv, output, registry);

    return true;
}

const RecipeDef* findCraftable(const Inventory& inv, const RecipeRegistry& recipes,
                               const ItemRegistry& registry)
{
    for (const auto& recipe : recipes.recipes)
    {
        if (canCraft(inv, recipe, registry))
            return &recipe;
    }
    return nullptr;
}

} // namespace engine::ops::crafting
