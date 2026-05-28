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

int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& slot : inv.items)
    {
        if (slot.config_path == config_path)
            total += slot.quantity;
    }
    return total;
}

// Consume a quantity, smallest-quality stacks first (preserve best
// materials). Returns the sum of quality-tier-indices consumed for
// averaging the output quality.
int consumeItem(Inventory& inv, const std::string& config_path, int quantity)
{
    std::vector<int> matches;
    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
    {
        if (inv.items[i].config_path == config_path)
            matches.push_back(i);
    }

    std::sort(matches.begin(), matches.end(),
              [&inv](int a, int b) { return inv.items[a].quality < inv.items[b].quality; });

    int remaining = quantity;
    int quality_sum = 0;
    for (const int idx : matches)
    {
        if (remaining <= 0)
            break;
        const int take = std::min(remaining, inv.items[idx].quantity);
        quality_sum += static_cast<int>(inv.items[idx].quality) * take;
        inv.items[idx].quantity -= take;
        remaining -= take;
    }

    // Remove empty stacks (reverse order preserves indices).
    for (int i = static_cast<int>(inv.items.size()) - 1; i >= 0; --i)
    {
        if (inv.items[i].quantity <= 0 && inv.items[i].config_path == config_path)
            inv.items.erase(inv.items.begin() + i);
    }

    return quality_sum;
}

} // namespace

bool canCraft(const Inventory& inv, const RecipeDef& recipe, const ItemRegistry& /*registry*/)
{
    for (const auto& ing : recipe.inputs)
    {
        if (countItem(inv, ing.config_path) < ing.quantity)
            return false;
    }
    return true;
}

bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry,
           bool free_materials)
{
    if (!canCraft(inv, recipe, registry))
        return false;

    // Pre-check: ensure output has room before consuming materials.
    // Consuming inputs frees slots, so check pessimistically (current
    // state). Stackable outputs that can merge into an existing stack
    // always succeed.
    const ItemDef* out_def = registry.find(recipe.output_item);
    const bool output_stackable = out_def != nullptr && out_def->stackable;
    bool output_can_merge = false;
    if (output_stackable)
    {
        const int max_stack = out_def->max_stack;
        int free_space = 0;
        for (const auto& slot : inv.items)
        {
            if (slot.config_path == recipe.output_item && slot.quantity < max_stack)
                free_space += max_stack - slot.quantity;
        }
        output_can_merge = free_space >= recipe.output_quantity;
    }
    if (!output_can_merge && static_cast<int>(inv.items.size()) >= inv.max_slots)
        return false;

    // Consume ingredients, tracking quality for output.
    int total_quality = 0;
    int total_items = 0;
    if (!free_materials)
    {
        for (const auto& ing : recipe.inputs)
        {
            total_quality += consumeItem(inv, ing.config_path, ing.quantity);
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
