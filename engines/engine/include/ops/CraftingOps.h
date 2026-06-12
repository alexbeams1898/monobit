#pragma once

#include "ecs/Items.h"

namespace engine::ops::crafting
{

using engine::ecs::Inventory;
using engine::ecs::ItemRegistry;
using engine::ecs::RecipeDef;
using engine::ecs::RecipeRegistry;

// True if every ingredient is present (sum across stacks).
bool canCraft(const Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry);

// Consume ingredients (lowest-quality first) and add the output item.
// Output quality is the rounded average of input qualities. Returns
// false if `canCraft` fails or inventory has no room for the output.
// `free_materials=true` skips the consume step (god mode).
bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry,
           bool free_materials = false);

// First recipe whose ingredients are present, or nullptr.
const RecipeDef* findCraftable(const Inventory& inv, const RecipeRegistry& recipes,
                               const ItemRegistry& registry);

} // namespace engine::ops::crafting
