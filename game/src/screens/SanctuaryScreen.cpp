#include "screens/SanctuaryScreen.h"

#include "UIRenderer.h"
#include "ecs/Components.h"
#include "ecs/EntityManager.h"
#include "ecs/GameComponents.h"
#include "ecs/GameConfig.h"
#include "ops/InventoryOps.h"
#include "screens/CraftingScreen.h"
#include "screens/MenuDialog.h"
#include "systems/NotificationSystem.h"

#include <SDL.h>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

static FontHandle sBodyFont = INVALID_FONT;
static FontHandle sTitleFont = INVALID_FONT;
static int sSel = 0;

static void doEvolve(EntityManager& em, entt::entity player)
{
    auto& reg = em.registry();
    if (!reg.all_of<Equipment, Inventory, WeaponXP>(player))
    {
        NotificationSystem::push("No weapon equipped", {0.8f, 0.4f, 0.4f, 1.0f});
        return;
    }

    auto& equip = reg.get<Equipment>(player);
    auto& inv = reg.get<Inventory>(player);
    auto& wxp = reg.get<WeaponXP>(player);
    const auto& evoReg = reg.ctx().get<EvolutionRegistry>();
    const auto& items = reg.ctx().get<ItemRegistry>();
    const auto& formulas = reg.ctx().get<FormulaConfig>();

    if (equip.main_hand.empty())
    {
        NotificationSystem::push("No weapon equipped", {0.8f, 0.4f, 0.4f, 1.0f});
        return;
    }

    auto it = evoReg.weapon_to_node.find(equip.main_hand.config_path);
    if (it == evoReg.weapon_to_node.end())
    {
        NotificationSystem::push("No evolutions available", {0.6f, 0.6f, 0.6f, 1.0f});
        return;
    }

    const int familyIdx = it->second.first;
    const std::string& nodeId = it->second.second;
    const auto& family = evoReg.families[familyIdx];
    auto nodeIt = family.nodes.find(nodeId);
    if (nodeIt == family.nodes.end() || nodeIt->second.evolutions.empty())
    {
        NotificationSystem::push("No evolutions available", {0.6f, 0.6f, 0.6f, 1.0f});
        return;
    }

    for (const auto& path : nodeIt->second.evolutions)
    {
        if (!InventoryOps::canEvolve(inv, equip, wxp, path))
            continue;

        auto targetIt = family.nodes.find(path.target_node);
        if (targetIt == family.nodes.end())
            continue;

        const std::string& newConfig = targetIt->second.weapon_config_path;
        if (InventoryOps::evolveWeapon(inv, equip, wxp, path, newConfig, items,
                                       formulas.weapon_xp.carry_factor))
        {
            auto& compendium = reg.ctx().get<Compendium>();
            compendium.discover(newConfig);

            const ItemDef* def = items.find(newConfig);
            const std::string name = (def != nullptr) ? def->name : "weapon";
            NotificationSystem::push("Evolved to " + name + "!", {0.9f, 0.85f, 0.4f, 1.0f});
            return;
        }
    }

    NotificationSystem::push("Cannot evolve yet (level/materials)", {0.8f, 0.6f, 0.3f, 1.0f});
}

void SanctuaryScreen::init(FontHandle body_font, FontHandle title_font)
{
    sBodyFont = body_font;
    sTitleFont = title_font;
}

void SanctuaryScreen::reset()
{
    sSel = -1;
}

void SanctuaryScreen::render(EntityManager& em, int window_w, int window_h)
{
    ZoneScopedN("SanctuaryScreen");

    const float ww = static_cast<float>(window_w);
    const float wh = static_cast<float>(window_h);
    auto& ui = em.registry().ctx().get<UIState>();

    entt::entity player = entt::null;
    for (auto e : em.registry().view<PlayerActions>())
    {
        player = e;
        break;
    }
    if (player == entt::null)
    {
        ui.active_screen = UIState::Screen::None;
        return;
    }

    // Build menu options.
    std::vector<MenuDialog::Option> options;

    // 1. Evolve Weapon
    bool canEvo = false;
    if (em.registry().all_of<Equipment, Inventory, WeaponXP>(player))
    {
        const auto& equip = em.registry().get<Equipment>(player);
        if (!equip.main_hand.empty())
        {
            const auto& evoReg = em.registry().ctx().get<EvolutionRegistry>();
            auto it = evoReg.weapon_to_node.find(equip.main_hand.config_path);
            if (it != evoReg.weapon_to_node.end())
            {
                const auto& family = evoReg.families[it->second.first];
                auto nodeIt = family.nodes.find(it->second.second);
                if (nodeIt != family.nodes.end())
                {
                    const auto& inv = em.registry().get<Inventory>(player);
                    const auto& wxp = em.registry().get<WeaponXP>(player);
                    for (const auto& path : nodeIt->second.evolutions)
                    {
                        if (InventoryOps::canEvolve(inv, equip, wxp, path))
                        {
                            canEvo = true;
                            break;
                        }
                    }
                }
            }
        }
    }
    options.push_back(
        {"Evolve Weapon", canEvo ? "Transform your weapon" : "No evolutions ready", canEvo});

    // 2. Craft
    options.push_back({"Craft", "Open crafting menu", true});

    // 3. Leave
    options.push_back({"Leave", "Close sanctuary menu", true});

    MenuDialog::Options dlgOpts;
    dlgOpts.title_font = sTitleFont;
    dlgOpts.body_font = sBodyFont;
    dlgOpts.title = "Sanctuary";
    dlgOpts.items = options;
    dlgOpts.hint = "[Enter/Click] Select   [Esc/RMB] Leave";
    dlgOpts.selection = &sSel;

    auto result = MenuDialog::render(em, dlgOpts, ww, wh);

    if (result.dismissed)
    {
        ui.active_screen = UIState::Screen::None;
        return;
    }

    if (result.selected >= 0)
    {
        const auto& opt = options[result.selected];
        if (opt.label == "Evolve Weapon")
            doEvolve(em, player);
        else if (opt.label == "Craft")
        {
            CraftingScreen::reset();
            ui.active_screen = UIState::Screen::Crafting;
        }
        else if (opt.label == "Leave")
            ui.active_screen = UIState::Screen::None;
    }
}
