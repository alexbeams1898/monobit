#include "systems/CraftingSystem.h"

#include "ecs/Components.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/CraftingOps.h"
#include "systems/AudioSystem.h"
#include "systems/NotificationSystem.h"

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
            continue;

        const bool godMode = reg.ctx().get<DebugFlags>().god_mode;
        if (CraftingOps::craft(inv, *recipe, items, godMode))
        {
            TracyMessageL("ItemCrafted");
            const auto& snd = reg.ctx().get<SoundConfig>();
            {
                const auto& pk = snd.get("pickup");
                AudioSystem::playSfx(pk.path, pk.volume);
            }
            NotificationSystem::push("Crafted " + recipe->name + "!", {0.3f, 0.9f, 0.3f, 1.0f});
        }
    }
}
