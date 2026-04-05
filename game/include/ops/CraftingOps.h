#pragma once

#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"

namespace CraftingOps
{

// Check if the player has all ingredients.
bool canCraft(const Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry);

// Consume ingredients and add the output item. Returns false if canCraft fails
// or inventory is full for the output. If free_materials is true, ingredients
// are not consumed (god mode).
bool craft(Inventory& inv, const RecipeDef& recipe, const ItemRegistry& registry,
           bool free_materials = false);

// Return a pointer to the first craftable recipe, or nullptr.
const RecipeDef* findCraftable(const Inventory& inv, const RecipeRegistry& recipes,
                               const ItemRegistry& registry);

} // namespace CraftingOps
