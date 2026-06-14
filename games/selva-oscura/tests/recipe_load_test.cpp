// Test that Selva's loadRecipeDirectory wraps the engine loader
// correctly and populates the process-wide RecipeRegistry from the
// synced config dir. The test target's CWD is build/bin/<game>/ where
// config/ is synced, so config/recipes resolves to the real shipped
// recipes.

#include "ecs/Items.h"
#include "items/ItemRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("loadRecipeDirectory populates the registry from disk", "[recipes][load]")
{
    selva::items::loadRecipeDirectory("config/recipes");
    const auto& reg = selva::items::recipeRegistry();
    REQUIRE_FALSE(reg.recipes.empty());

    // The shipped distill_larval_residue recipe must be present with
    // the expected inputs / output.
    const auto it = std::find_if(reg.recipes.begin(), reg.recipes.end(),
                                  [](const engine::ecs::RecipeDef& r) {
                                      return r.name == "Compact residue";
                                  });
    REQUIRE(it != reg.recipes.end());
    REQUIRE(it->output_item == "config/items/materials/refined_residue.json");
    REQUIRE(it->output_quantity == 1);
    REQUIRE(it->inputs.size() == 1);
    REQUIRE(it->inputs[0].config_path == "config/items/materials/larval_residue.json");
    REQUIRE(it->inputs[0].quantity == 3);
}

TEST_CASE("loadRecipeDirectory clears prior state on second load", "[recipes][load]")
{
    selva::items::loadRecipeDirectory("config/recipes");
    const std::size_t first_count = selva::items::recipeRegistry().recipes.size();
    REQUIRE(first_count > 0);
    selva::items::loadRecipeDirectory("config/recipes");
    const std::size_t second_count = selva::items::recipeRegistry().recipes.size();
    REQUIRE(second_count == first_count);
}
