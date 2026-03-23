#include "systems/CraftingSystem.h"

#include "CraftingOps.h"
#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "systems/AudioSystem.h"
#include "systems/NotificationSystem.h"

#include <iostream>
#include <tracy/Tracy.hpp>

void CraftingSystem::update(EntityManager& em)
{
    ZoneScopedN("CraftingSystem");
    auto& reg = em.registry();

    for (auto [entity, actions, inv] : reg.view<PlayerActions, Inventory>().each())
    {
        if (!actions.craft)
            continue;

        const auto& recipes = reg.ctx().get<RecipeRegistry>();
        const auto& items = reg.ctx().get<ItemRegistry>();

        const RecipeDef* recipe = CraftingOps::findCraftable(inv, recipes, items);
        if (recipe == nullptr)
        {
            std::cout << "[CraftingSystem] No craftable recipes available.\n";
            continue;
        }

        if (CraftingOps::craft(inv, *recipe, items))
        {
            TracyMessageL("ItemCrafted");
            const auto& snd = reg.ctx().get<SoundConfig>();
            AudioSystem::playSfx(snd.pickup.path, snd.pickup.volume);
            NotificationSystem::push("Crafted " + recipe->name + "!", {0.3f, 0.9f, 0.3f, 1.0f});
        }
    }
}
