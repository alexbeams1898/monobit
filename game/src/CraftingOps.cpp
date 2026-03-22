#include "CraftingOps.h"

#include "InventoryOps.h"

#include <algorithm>

namespace CraftingOps
{

// Count how many of a given item are in the inventory (summing stacks).
static int countItem(const Inventory& inv, const std::string& config_path)
{
    int total = 0;
    for (const auto& slot : inv.items)
    {
        if (slot.config_path == config_path)
            total += slot.quantity;
    }
    return total;
}

bool canCraft(const Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry)
{
    (void)registry;

    for (const auto& ing : recipe.inputs)
    {
        if (countItem(inv, ing.config_path) < ing.quantity)
            return false;
    }
    return true;
}

// Remove a specific quantity of an item from inventory, consuming across stacks.
// Returns the sum of quality tier indices consumed (for averaging).
static int consumeItem(Inventory& inv, const std::string& config_path, int quantity)
{
    int remaining = quantity;
    int qualitySum = 0;
    for (int i = 0; i < static_cast<int>(inv.items.size()) && remaining > 0; ++i)
    {
        if (inv.items[i].config_path != config_path)
            continue;

        const int take = std::min(remaining, inv.items[i].quantity);
        qualitySum += static_cast<int>(inv.items[i].quality) * take;
        inv.items[i].quantity -= take;
        remaining -= take;

        if (inv.items[i].quantity <= 0)
        {
            inv.items.erase(inv.items.begin() + i);
            --i;
        }
    }
    return qualitySum;
}

bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry)
{
    if (!canCraft(inv, recipe, registry))
        return false;

    // Consume ingredients, tracking quality for output.
    int totalQuality = 0;
    int totalItems = 0;
    for (const auto& ing : recipe.inputs)
    {
        totalQuality += consumeItem(inv, ing.config_path, ing.quantity);
        totalItems += ing.quantity;
    }

    // Output quality = rounded average of input qualities.
    const int avgQuality = (totalItems > 0) ? ((totalQuality + totalItems / 2) / totalItems) : 1;
    const auto outputQuality =
        static_cast<QualityTier>(std::min(avgQuality, static_cast<int>(QualityTier::Masterwork)));

    // Add output.
    ItemInstance output;
    output.config_path = recipe.output_item;
    output.quantity = recipe.output_quantity;
    output.quality = outputQuality;

    if (!InventoryOps::addItem(inv, output, registry))
        return false;

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

} // namespace CraftingOps
