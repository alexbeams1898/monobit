#include "ops/CraftingOps.h"

#include "ops/InventoryOps.h"

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
// Uses lowest quality stacks first so the player's best materials are preserved.
// Returns the sum of quality tier indices consumed (for averaging output quality).
static int consumeItem(Inventory& inv, const std::string& config_path, int quantity)
{
    // Gather matching stack indices sorted by quality (worst first).
    std::vector<int> matches;
    for (int i = 0; i < static_cast<int>(inv.items.size()); ++i)
        if (inv.items[i].config_path == config_path)
            matches.push_back(i);

    std::sort(matches.begin(), matches.end(),
              [&inv](int a, int b) { return inv.items[a].quality < inv.items[b].quality; });

    int remaining = quantity;
    int qualitySum = 0;
    for (const int idx : matches)
    {
        if (remaining <= 0)
            break;
        const int take = std::min(remaining, inv.items[idx].quantity);
        qualitySum += static_cast<int>(inv.items[idx].quality) * take;
        inv.items[idx].quantity -= take;
        remaining -= take;
    }

    // Remove empty stacks (reverse order preserves indices).
    for (int i = static_cast<int>(inv.items.size()) - 1; i >= 0; --i)
        if (inv.items[i].quantity <= 0 && inv.items[i].config_path == config_path)
            inv.items.erase(inv.items.begin() + i);

    return qualitySum;
}

bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry,
           bool free_materials)
{
    if (!canCraft(inv, recipe, registry))
        return false;

    // Pre-check: ensure output has room before consuming materials.
    // Consuming inputs frees slots, so check pessimistically (current state).
    // Stackable outputs that can merge into an existing stack always succeed.
    const ItemDef* outDef = registry.find(recipe.output_item);
    const bool outputStackable = outDef != nullptr && outDef->stackable;
    bool outputCanMerge = false;
    if (outputStackable)
    {
        const int maxStack = outDef->max_stack;
        int freeSpace = 0;
        for (const auto& slot : inv.items)
        {
            if (slot.config_path == recipe.output_item && slot.quantity < maxStack)
                freeSpace += maxStack - slot.quantity;
        }
        outputCanMerge = freeSpace >= recipe.output_quantity;
    }
    if (!outputCanMerge && static_cast<int>(inv.items.size()) >= inv.max_slots)
        return false;

    // Consume ingredients, tracking quality for output.
    int totalQuality = 0;
    int totalItems = 0;
    if (!free_materials)
    {
        for (const auto& ing : recipe.inputs)
        {
            totalQuality += consumeItem(inv, ing.config_path, ing.quantity);
            totalItems += ing.quantity;
        }
    }

    // Output quality = rounded average of input qualities.
    const int avgQuality = (totalItems > 0) ? ((totalQuality + totalItems / 2) / totalItems) : 1;
    const auto outputQuality =
        static_cast<QualityTier>(std::min(avgQuality, static_cast<int>(QualityTier::Masterwork)));

    // Add output (guaranteed to succeed: materials freed slots or pre-check passed).
    ItemInstance output;
    output.config_path = recipe.output_item;
    output.quantity = recipe.output_quantity;
    output.quality = outputQuality;
    InventoryOps::addItem(inv, output, registry);

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
